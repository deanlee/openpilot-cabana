#include "thumbnail_cache.h"

#include <QMetaObject>
#include <algorithm>
#include <cstring>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include "modules/system/stream_manager.h"
#include "replay/include/config.h"
#include "replay/include/filereader.h"
#include "replay/include/framereader.h"
#include "replay/include/replay.h"
#include "replay/include/route.h"
#include "replay/include/seg_mgr.h"
#include "src/core/streams/replay_stream.h"

namespace {

constexpr int kSegmentSeconds = 60;

CameraType cameraTypeFor(VisionStreamType s) {
  switch (s) {
    case VISION_STREAM_DRIVER: return DriverCam;
    case VISION_STREAM_WIDE_ROAD: return WideRoadCam;
    default: return RoadCam;
  }
}

ReplayStream* replayStream() {
  return qobject_cast<ReplayStream*>(StreamManager::stream());
}

// Resolve a local on-disk path for a segment's video file, mirroring the logic
// in Segment::Segment() / FrameReader::load(). Returns empty string if the file
// is not available for this segment.
std::string segmentVideoPath(const SegmentFile& sf, CameraType cam, uint32_t flags) {
  std::string url;
  if (cam == RoadCam) {
    url = ((flags & REPLAY_FLAG_QCAMERA) || sf.road_cam.empty()) ? sf.qcamera : sf.road_cam;
  } else if (cam == DriverCam) {
    url = sf.driver_cam;
  } else if (cam == WideRoadCam) {
    url = sf.wide_road_cam;
  }
  if (url.empty()) return {};
  return url.find("https://") == 0 ? cacheFilePath(url) : url;
}

}  // namespace

// =========================================================================
// KeyframeDecoder: a self-contained FFmpeg pipeline that decodes one keyframe
// at a time from a single segment file. Owned exclusively by the worker
// thread.
// =========================================================================
class KeyframeDecoder {
 public:
  ~KeyframeDecoder() {
    if (sws_ctx_) sws_freeContext(sws_ctx_);
    if (codec_ctx_) avcodec_free_context(&codec_ctx_);
    if (fmt_ctx_) avformat_close_input(&fmt_ctx_);
  }

  bool open(const std::string& file) {
    if (avformat_open_input(&fmt_ctx_, file.c_str(), nullptr, nullptr) != 0) return false;
    if (avformat_find_stream_info(fmt_ctx_, nullptr) < 0) return false;
    video_stream_idx_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_idx_ < 0) return false;

    auto* codecpar = fmt_ctx_->streams[video_stream_idx_]->codecpar;
    const AVCodec* dec = avcodec_find_decoder(codecpar->codec_id);
    if (!dec) return false;
    codec_ctx_ = avcodec_alloc_context3(dec);
    if (!codec_ctx_) return false;
    if (avcodec_parameters_to_context(codec_ctx_, codecpar) != 0) return false;
    // Skip non-keyframes entirely — this is the core efficiency win.
    codec_ctx_->skip_frame = AVDISCARD_NONKEY;
    if (avcodec_open2(codec_ctx_, dec, nullptr) < 0) return false;
    width_ = codec_ctx_->width;
    height_ = codec_ctx_->height;
    return true;
  }

  // Build the list of keyframe packet indices/positions by referencing the
  // already-populated packets_info from the playback FrameReader (avoids
  // scanning the file ourselves).
  void setKeyframes(const std::vector<FrameReader::PacketInfo>& packets) {
    keyframes_.clear();
    keyframes_.reserve(packets.size() / 30 + 1);
    for (int i = 0; i < (int)packets.size(); ++i) {
      if (packets[i].flags & AV_PKT_FLAG_KEY) {
        keyframes_.push_back({i, packets[i].pos});
      }
    }
    total_packets_ = packets.size();
  }

  // Decode the keyframe nearest at-or-before `seconds_in_segment` and return
  // its index (in original packet stream). Output goes into *out as an RGB
  // QImage at the requested height. Returns -1 on failure.
  int decodeNearestKeyframe(double seconds_in_segment, int target_height, QImage* out) {
    if (keyframes_.empty() || total_packets_ <= 0) return -1;

    // Estimate target packet idx by linear time-in-segment mapping. This does
    // not need to be exact — we always snap to a keyframe.
    int est_idx = std::clamp(
        (int)(seconds_in_segment / (double)kSegmentSeconds * total_packets_),
        0, total_packets_ - 1);

    // Binary search for nearest keyframe <= est_idx.
    auto it = std::upper_bound(keyframes_.begin(), keyframes_.end(), est_idx,
                               [](int v, const KF& k) { return v < k.idx; });
    if (it == keyframes_.begin()) {
      // est_idx is before the first keyframe — use the first one anyway.
      it = keyframes_.begin() + 1;
    }
    --it;
    const KF kf = *it;

    if (!decodeAtPos(kf.pos, out, target_height)) return -1;
    return kf.idx;
  }

 private:
  struct KF { int idx; int64_t pos; };

  bool decodeAtPos(int64_t pos, QImage* out, int target_height) {
    if (avformat_seek_file(fmt_ctx_, video_stream_idx_, pos, pos, pos, AVSEEK_FLAG_BYTE) < 0) {
      return false;
    }
    avcodec_flush_buffers(codec_ctx_);

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!pkt || !frame) {
      if (pkt) av_packet_free(&pkt);
      if (frame) av_frame_free(&frame);
      return false;
    }

    bool ok = false;
    while (av_read_frame(fmt_ctx_, pkt) >= 0) {
      if (pkt->stream_index != video_stream_idx_) {
        av_packet_unref(pkt);
        continue;
      }
      if (avcodec_send_packet(codec_ctx_, pkt) < 0) {
        av_packet_unref(pkt);
        break;
      }
      av_packet_unref(pkt);

      int r = avcodec_receive_frame(codec_ctx_, frame);
      if (r == AVERROR(EAGAIN)) continue;
      if (r < 0) break;

      ok = frameToImage(frame, target_height, out);
      av_frame_unref(frame);
      break;
    }
    av_packet_free(&pkt);
    av_frame_free(&frame);
    return ok;
  }

  bool frameToImage(AVFrame* frame, int target_height, QImage* out) {
    int dst_h = target_height > 0 ? target_height : height_;
    int dst_w = std::max(1, (int)((double)width_ * dst_h / (double)height_));
    // Force even width — some swscale code paths dislike odd alignment.
    if (dst_w & 1) ++dst_w;

    sws_ctx_ = sws_getCachedContext(sws_ctx_,
                                    width_, height_, (AVPixelFormat)frame->format,
                                    dst_w, dst_h, AV_PIX_FMT_RGB24,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx_) return false;

    QImage img(dst_w, dst_h, QImage::Format_RGB888);
    uint8_t* dst[1] = {img.bits()};
    int dst_stride[1] = {(int)img.bytesPerLine()};
    sws_scale(sws_ctx_, frame->data, frame->linesize, 0, height_, dst, dst_stride);
    *out = std::move(img);
    return true;
  }

  AVFormatContext* fmt_ctx_ = nullptr;
  AVCodecContext* codec_ctx_ = nullptr;
  SwsContext* sws_ctx_ = nullptr;
  int video_stream_idx_ = -1;
  int width_ = 0, height_ = 0;
  int total_packets_ = 0;
  std::vector<KF> keyframes_;
};

// =========================================================================
// ThumbnailCache
// =========================================================================

ThumbnailCache::ThumbnailCache(QObject* parent) : QObject(parent) {
  moveToThread(&worker_thread_);
  worker_thread_.start(QThread::LowPriority);
}

ThumbnailCache::~ThumbnailCache() {
  // Make sure all owned FFmpeg objects are destroyed on the worker thread.
  QMetaObject::invokeMethod(this, "handleClear", Qt::BlockingQueuedConnection);
  worker_thread_.quit();
  worker_thread_.wait();
}

void ThumbnailCache::setStreamType(VisionStreamType type) {
  if (stream_type_ == type) return;
  stream_type_ = type;
  clear();
}

void ThumbnailCache::requestThumbnail(double seconds, int target_height) {
  quint64 token = latest_token_.fetch_add(1, std::memory_order_relaxed) + 1;
  QMetaObject::invokeMethod(this, "handleRequest", Qt::QueuedConnection,
                            Q_ARG(quint64, token),
                            Q_ARG(double, seconds),
                            Q_ARG(int, target_height));
}

void ThumbnailCache::clear() {
  QMetaObject::invokeMethod(this, "handleClear", Qt::QueuedConnection);
}

void ThumbnailCache::handleClear() {
  decoders_.clear();
  decoder_lru_.clear();
  pixmap_cache_.clear();
  pixmap_lru_.clear();
}

void ThumbnailCache::handleRequest(quint64 token, double seconds, int target_height) {
  // Drop stale requests as early as possible.
  if (token != latest_token_.load(std::memory_order_relaxed)) return;

  auto* rs = replayStream();
  if (!rs) return;
  Replay* replay = rs->getReplay();
  if (!replay) return;

  int seg_num = std::max(0, (int)(seconds / kSegmentSeconds));
  double sec_in_seg = seconds - seg_num * kSegmentSeconds;

  auto event_data = replay->getEventData();
  if (!event_data || !event_data->isSegmentLoaded(seg_num)) return;
  auto seg_it = event_data->segments.find(seg_num);
  if (seg_it == event_data->segments.end()) return;
  auto segment = seg_it->second;
  if (!segment) return;

  CameraType cam = cameraTypeFor(stream_type_);
  FrameReader* playback_fr = segment->frames[cam].get();
  if (!playback_fr || playback_fr->packets_info.empty()) return;

  // Try cache. We don't know the keyframe index until we resolve it, but the
  // estimate is deterministic — replicate it cheaply for cache lookup.
  // (We could also cache by est_idx, but kf_idx is stable and dedupes adjacent
  //  hovers within the same GOP.)

  std::string preferred_path;
  if (playback_fr->input_ctx && playback_fr->input_ctx->url) {
    preferred_path = playback_fr->input_ctx->url;
  }

  KeyframeDecoder* dec = decoderForSegment(seg_num, preferred_path);
  if (!dec) return;

  // Pre-stage keyframes (cheap): pointer to playback_fr->packets_info is
  // stable for the segment's lifetime.
  dec->setKeyframes(playback_fr->packets_info);

  // Quick estimate path for cache check (mirror decoder estimate).
  int total = (int)playback_fr->packets_info.size();
  int est_idx = std::clamp(
      (int)(sec_in_seg / (double)kSegmentSeconds * total), 0, total - 1);
  int kf_idx_guess = -1;
  for (int i = est_idx; i >= 0; --i) {
    if (playback_fr->packets_info[i].flags & AV_PKT_FLAG_KEY) {
      kf_idx_guess = i;
      break;
    }
  }
  if (kf_idx_guess >= 0) {
    QPixmap cached;
    if (lookupPixmap(seg_num, kf_idx_guess, target_height, &cached)) {
      // Bail again if a newer request arrived while we were checking.
      if (token != latest_token_.load(std::memory_order_relaxed)) return;
      emit thumbnailReady(seconds, cached);
      return;
    }
  }

  // Re-check token before doing real work (decode can take a few ms).
  if (token != latest_token_.load(std::memory_order_relaxed)) return;

  QImage img;
  int kf_idx = dec->decodeNearestKeyframe(sec_in_seg, target_height, &img);
  if (kf_idx < 0 || img.isNull()) return;

  // Final stale check before publishing — avoids overwriting the UI with an
  // out-of-date thumbnail when the user has already moved on.
  if (token != latest_token_.load(std::memory_order_relaxed)) return;

  QPixmap pix = QPixmap::fromImage(std::move(img));
  cachePixmap(seg_num, kf_idx, target_height, pix);
  emit thumbnailReady(seconds, pix);
}

KeyframeDecoder* ThumbnailCache::decoderForSegment(int seg_num, const std::string& preferred_path) {
  auto it = decoders_.find(seg_num);
  if (it != decoders_.end()) {
    touchDecoderLRU(seg_num);
    return it->second.get();
  }

  std::string path = preferred_path;
  if (path.empty()) {
    auto* rs = replayStream();
    if (!rs || !rs->getReplay()) return nullptr;
    const Route& route = rs->getReplay()->route();
    auto& seg_map = route.segments();
    auto sit = seg_map.find(seg_num);
    if (sit == seg_map.end()) return nullptr;

    // Fallback: when input_ctx URL is not available, approximate the same file
    // selection used by playback.
    path = segmentVideoPath(sit->second, cameraTypeFor(stream_type_), 0);
  }
  if (path.empty() || !util::file_exists(path)) return nullptr;

  auto dec = std::make_unique<KeyframeDecoder>();
  if (!dec->open(path)) return nullptr;
  auto* raw = dec.get();
  decoders_.emplace(seg_num, std::move(dec));
  decoder_lru_.push_back(seg_num);
  evictDecoders();
  return raw;
}

void ThumbnailCache::touchDecoderLRU(int seg_num) {
  decoder_lru_.remove(seg_num);
  decoder_lru_.push_back(seg_num);
}

void ThumbnailCache::evictDecoders() {
  while ((int)decoders_.size() > kMaxDecoders && !decoder_lru_.empty()) {
    int victim = decoder_lru_.front();
    decoder_lru_.pop_front();
    decoders_.erase(victim);
  }
}

void ThumbnailCache::cachePixmap(int seg_num, int kf_idx, int height, const QPixmap& pix) {
  CacheKey key{seg_num, kf_idx, height};
  auto it = pixmap_cache_.find(key);
  if (it != pixmap_cache_.end()) {
    it->second = pix;
    pixmap_lru_.remove(key);
    pixmap_lru_.push_back(key);
    return;
  }
  pixmap_cache_.emplace(key, pix);
  pixmap_lru_.push_back(key);
  while ((int)pixmap_cache_.size() > kMaxPixmaps && !pixmap_lru_.empty()) {
    pixmap_cache_.erase(pixmap_lru_.front());
    pixmap_lru_.pop_front();
  }
}

bool ThumbnailCache::lookupPixmap(int seg_num, int kf_idx, int height, QPixmap* out) {
  auto it = pixmap_cache_.find({seg_num, kf_idx, height});
  if (it == pixmap_cache_.end()) return false;
  *out = it->second;
  // Move to back of LRU.
  pixmap_lru_.remove({seg_num, kf_idx, height});
  pixmap_lru_.push_back({seg_num, kf_idx, height});
  return true;
}
