#pragma once

#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>

#include "msgq/visionipc/visionipc_client.h"

class KeyframeDecoder;

// Async, on-demand thumbnail service.
//
// - Lives on its own worker thread; the public API may be called from the GUI
//   thread.
// - Decodes ONLY the keyframe at or before the requested time using its own
//   FFmpeg AVFormatContext / AVCodecContext per segment (does not share state
//   with the playback decoder).
// - Coalesces rapid hover requests via an atomic token: only the latest
//   pending request is decoded; older ones are dropped.
// - Caches resulting QPixmaps in an LRU keyed by (segment, keyframe_index,
//   target_height).
class ThumbnailCache : public QObject {
  Q_OBJECT
 public:
  explicit ThumbnailCache(QObject* parent = nullptr);
  ~ThumbnailCache() override;

  // Camera selection for thumbnails (defaults to RoadCam/qcamera).
  void setStreamType(VisionStreamType type);

  // Request a thumbnail for the given playback time. Safe to call from any
  // thread; emits thumbnailReady on the GUI thread when decoding completes.
  // target_height is the desired QPixmap height in pixels.
  void requestThumbnail(double seconds, int target_height);

  // Drop all cached pixmaps and close all open decoders. Call on stream
  // change.
  void clear();

 signals:
  void thumbnailReady(double seconds, QPixmap pixmap);

 private:
  Q_INVOKABLE void handleRequest(quint64 token, double seconds, int target_height);
  Q_INVOKABLE void handleClear();

  KeyframeDecoder* decoderForSegment(int seg_num, const std::string& preferred_path);
  void touchDecoderLRU(int seg_num);
  void evictDecoders();
  void cachePixmap(int seg_num, int kf_idx, int height, const QPixmap& pix);
  bool lookupPixmap(int seg_num, int kf_idx, int height, QPixmap* out);

  QThread worker_thread_;
  std::atomic<quint64> latest_token_{0};
  VisionStreamType stream_type_ = VISION_STREAM_ROAD;

  // Worker-thread owned state (no locking required: only handleRequest /
  // handleClear touch these and both run on worker_thread_).
  std::unordered_map<int, std::unique_ptr<KeyframeDecoder>> decoders_;
  std::list<int> decoder_lru_;  // back = most recent
  static constexpr int kMaxDecoders = 4;

  struct CacheKey {
    int seg_num;
    int kf_idx;
    int height;
    bool operator==(const CacheKey& o) const noexcept {
      return seg_num == o.seg_num && kf_idx == o.kf_idx && height == o.height;
    }
  };
  struct CacheKeyHash {
    size_t operator()(const CacheKey& k) const noexcept {
      size_t h = static_cast<size_t>(k.seg_num) * 0x9E3779B97F4A7C15ULL;
      h ^= static_cast<size_t>(k.kf_idx) + (h << 6) + (h >> 2);
      h ^= static_cast<size_t>(k.height) + (h << 6) + (h >> 2);
      return h;
    }
  };
  std::unordered_map<CacheKey, QPixmap, CacheKeyHash> pixmap_cache_;
  std::list<CacheKey> pixmap_lru_;
  static constexpr int kMaxPixmaps = 256;
};
