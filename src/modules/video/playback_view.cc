#include "playback_view.h"

#include <QtConcurrent>

#include "modules/system/stream_manager.h"

static Replay *getReplay() {
  auto stream = qobject_cast<ReplayStream *>(StreamManager::stream());
  return stream ? stream->getReplay() : nullptr;
}

PlaybackCameraView::PlaybackCameraView(std::string stream_name, VisionStreamType stream_type, QWidget* parent)
    : CameraView(stream_name, stream_type, parent) {
  fade_animation = new QPropertyAnimation(this, "overlayOpacity");
  fade_animation->setDuration(500);
  fade_animation->setStartValue(0.2f);
  fade_animation->setEndValue(0.7f);
  fade_animation->setEasingCurve(QEasingCurve::InOutQuad);
  connect(fade_animation, &QPropertyAnimation::valueChanged, this, QOverload<>::of(&PlaybackCameraView::update));
}

void PlaybackCameraView::parseQLog(std::shared_ptr<LogReader> qlog) {
  std::mutex mutex;
  QtConcurrent::blockingMap(qlog->events.cbegin(), qlog->events.cend(), [this, &mutex](const Event& e) {
    if (e.which == cereal::Event::Which::THUMBNAIL) {
      capnp::FlatArrayMessageReader reader(e.data);
      auto thumb_data = reader.getRoot<cereal::Event>().getThumbnail();
      auto image_data = thumb_data.getThumbnail();
      if (QPixmap thumb; thumb.loadFromData(image_data.begin(), image_data.size(), "jpeg")) {
        QPixmap generated_thumb = generateThumbnail(thumb, StreamManager::stream()->toSeconds(thumb_data.getTimestampEof()));
        std::lock_guard lock(mutex);
        thumbnails[thumb_data.getTimestampEof()] = generated_thumb;
        big_thumbnails[thumb_data.getTimestampEof()] = thumb;
      }
    }
  });
  update();
}

void PlaybackCameraView::paintGL() {
  if (!StreamManager::instance().isReplayStream()) {
    glClearColor(bg.redF(), bg.greenF(), bg.blueF(), bg.alphaF());
    glClear(GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    return;
  }

  CameraView::paintGL();

  QPainter p(this);
  bool scrubbing = false;
  if (thumbnail_dispaly_time >= 0) {
    scrubbing = StreamManager::stream()->isPaused();
    scrubbing ? drawScrubThumbnail(p) : drawThumbnail(p);
  }
  if (auto alert = getReplay()->findAlertAtTime(scrubbing ? thumbnail_dispaly_time : StreamManager::stream()->currentSec())) {
    drawAlert(p, rect(), *alert);
  }

  if (StreamManager::stream()->isPaused()) {
    p.setPen(QColor(200, 200, 200, static_cast<int>(255 * fade_animation->currentValue().toFloat())));
    p.setFont(QFont(font().family(), 16, QFont::Bold));
    p.drawText(rect(), Qt::AlignCenter, tr("PAUSED"));
  } else if (!current_frame_) {
    p.setRenderHint(QPainter::Antialiasing);
    QColor gray(130, 130, 130);
    int icon_size = 32;
    QPixmap icon = utils::icon("video-off", QSize(icon_size, icon_size), gray);
    int cx = rect().center().x();
    int cy = rect().center().y();
    p.drawPixmap(cx - (icon_size / 2), cy - 27, icon);
    p.setPen(gray);
    p.setFont(QFont("sans-serif", 9, QFont::DemiBold));
    p.drawText(rect().adjusted(0, 25, 0, 25), Qt::AlignCenter, tr("No Video"));
  }
}

QPixmap PlaybackCameraView::generateThumbnail(QPixmap thumb, double seconds) {
  QPixmap scaled = thumb.scaledToHeight(MIN_VIDEO_HEIGHT - THUMBNAIL_MARGIN * 2, Qt::SmoothTransformation);
  QPainter p(&scaled);
  p.setPen(QPen(palette().color(QPalette::BrightText), 2));
  p.drawRect(scaled.rect());
  if (auto alert = getReplay()->findAlertAtTime(seconds)) {
    p.setFont(QFont(font().family(), 10));
    drawAlert(p, scaled.rect(), *alert);
  }
  return scaled;
}

void PlaybackCameraView::drawScrubThumbnail(QPainter& p) {
  p.fillRect(rect(), Qt::black);
  auto it = big_thumbnails.lowerBound(StreamManager::stream()->toMonoTime(thumbnail_dispaly_time));
  if (it != big_thumbnails.end()) {
    QPixmap scaled_thumb = it.value().scaled(rect().size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QRect thumb_rect(rect().center() - scaled_thumb.rect().center(), scaled_thumb.size());
    p.drawPixmap(thumb_rect.topLeft(), scaled_thumb);
    drawTime(p, thumb_rect, thumbnail_dispaly_time);
  }
}

void PlaybackCameraView::drawThumbnail(QPainter& p) {
  auto it = thumbnails.lowerBound(StreamManager::stream()->toMonoTime(thumbnail_dispaly_time));
  if (it != thumbnails.end()) {
    const QPixmap& thumb = it.value();
    auto [min_sec, max_sec] = StreamManager::stream()->timeRange().value_or(std::make_pair(StreamManager::stream()->minSeconds(), StreamManager::stream()->maxSeconds()));
    int pos = (thumbnail_dispaly_time - min_sec) * width() / (max_sec - min_sec);
    int x = std::clamp(pos - thumb.width() / 2, THUMBNAIL_MARGIN, width() - thumb.width() - THUMBNAIL_MARGIN + 1);
    int y = height() - thumb.height() - THUMBNAIL_MARGIN;

    p.drawPixmap(x, y, thumb);
    drawTime(p, QRect{x, y, thumb.width(), thumb.height()}, thumbnail_dispaly_time);
  }
}

void PlaybackCameraView::drawTime(QPainter& p, const QRect& rect, double seconds) {
  p.setPen(palette().color(QPalette::BrightText));
  p.setFont(QFont(font().family(), 10));
  p.drawText(rect.adjusted(0, 0, 0, -THUMBNAIL_MARGIN), Qt::AlignHCenter | Qt::AlignBottom, QString::number(seconds, 'f', 3));
}

void PlaybackCameraView::drawAlert(QPainter& p, const QRect& rect, const Timeline::Entry& alert) {
  p.setPen(QPen(palette().color(QPalette::BrightText), 2));
  QColor color = timeline_colors[int(alert.type)];
  color.setAlphaF(0.5);
  QString text = QString::fromStdString(alert.text1);
  if (!alert.text2.empty()) text += "\n" + QString::fromStdString(alert.text2);

  QRect text_rect = rect.adjusted(1, 1, -1, -1);
  QRect r = p.fontMetrics().boundingRect(text_rect, Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap, text);
  p.fillRect(text_rect.left(), r.top(), text_rect.width(), r.height(), color);
  p.drawText(text_rect, Qt::AlignTop | Qt::AlignHCenter | Qt::TextWordWrap, text);
}
