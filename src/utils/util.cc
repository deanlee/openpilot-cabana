#include "utils/util.h"

#include <unistd.h>

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QPalette>
#include <QPixmapCache>
#include <QStyle>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QtSvg/QSvgRenderer>
#include <QWidget>

#include "common/util.h"
#include "modules/settings/settings.h"

namespace utils {

bool isDarkTheme() {
  QColor windowColor = QApplication::palette().color(QPalette::Window);
  return windowColor.lightness() < 128;
}

QString doubleToString(double value, int precision) {
  if (value == 0.0) {
    return QStringLiteral("0");
  }

  QString s = QString::number(value, 'f', precision);

  int dotIdx = s.indexOf('.');
  if (dotIdx != -1) {
    int i = s.length() - 1;
    // Walk back to remove trailing zeros
    while (i > dotIdx && s[i] == '0') i--;
    // Remove the dot if no decimals remain
    if (i == dotIdx) i--;

    // Only truncate if we actually removed something
    if (i < s.length() - 1) {
      s.truncate(i + 1);
    }
  }

  if (s == "0" || s == "-0" || s.isEmpty()) {
    return QStringLiteral("0");
  }

  return s;
}

int num_decimals(double num) {
  QString s = doubleToString(num);
  int dotIdx = s.indexOf('.');
  // If there's no dot, there are no decimals
  return (dotIdx == -1) ? 0 : (s.length() - dotIdx - 1);
}

QPixmap icon(const QString& id, QSize size, std::optional<QColor> color) {
  QColor icon_color = color.value_or(isDarkTheme() ? QColor("#bbbbbb") : QColor("#333333"));
  QString key = QString("lucide_%1_%2_%3").arg(id).arg(size.width()).arg(icon_color.rgba(), 0, 16);

  QPixmap pm;
  if (!QPixmapCache::find(key, &pm)) {
    QString path = QString(":/assets/%1.svg").arg(id);
    QSvgRenderer renderer(path);

    if (!renderer.isValid()) {
      return QPixmap();
    }

    const qreal deviceRatio = qApp->devicePixelRatio();
    const QSize physicalSize = size * deviceRatio;
    pm = QPixmap(physicalSize);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p);

    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(pm.rect(), icon_color);
    p.end();

    pm.setDevicePixelRatio(deviceRatio);
    QPixmapCache::insert(key, pm);
  }
  return pm;
}

void setTheme(int theme) {
  auto style = QApplication::style();
  if (!style) return;

  static int prev_theme = 0;
  if (theme != prev_theme) {
    prev_theme = theme;
    QPalette new_palette;
    if (theme == DARK_THEME) {
      // "Darcula" like dark theme
      new_palette.setColor(QPalette::Window, QColor("#353535"));
      new_palette.setColor(QPalette::WindowText, QColor("#bbbbbb"));
      new_palette.setColor(QPalette::Base, QColor("#3c3f41"));
      new_palette.setColor(QPalette::AlternateBase, QColor("#3c3f41"));
      new_palette.setColor(QPalette::ToolTipBase, QColor("#3c3f41"));
      new_palette.setColor(QPalette::ToolTipText, QColor("#bbb"));
      new_palette.setColor(QPalette::Text, QColor("#bbbbbb"));
      new_palette.setColor(QPalette::Button, QColor("#3c3f41"));
      new_palette.setColor(QPalette::ButtonText, QColor("#bbbbbb"));
      new_palette.setColor(QPalette::Highlight, QColor("#2f65ca"));
      new_palette.setColor(QPalette::HighlightedText, QColor("#bbbbbb"));
      new_palette.setColor(QPalette::BrightText, QColor("#f0f0f0"));
      new_palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#777777"));
      new_palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#777777"));
      new_palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#777777"));
      new_palette.setColor(QPalette::Light, QColor("#777777"));
      new_palette.setColor(QPalette::Dark, QColor("#353535"));
    } else {
      new_palette = style->standardPalette();
    }
    qApp->setPalette(new_palette);
    style->polish(qApp);
    for (auto w : QApplication::allWidgets()) {
      w->setPalette(new_palette);
    }
  }
}

QString formatSeconds(double sec, bool include_milliseconds, bool absolute_time) {
  if (absolute_time) {
    return QDateTime::fromMSecsSinceEpoch(sec * 1000).toString(include_milliseconds ? "yyyy-MM-dd HH:mm:ss.zzz" : "yyyy-MM-dd HH:mm:ss");
  }

  // High-performance relative time (math is faster than QTime objects)
  int total_ms = static_cast<int>(sec * 1000);
  int ms = total_ms % 1000;
  int total_s = total_ms / 1000;
  int s = total_s % 60;
  int m = (total_s / 60) % 60;
  int h = total_s / 3600;

  char buf[32];
  int len;
  if (h > 0) {
    len = include_milliseconds
              ? std::sprintf(buf, "%02d:%02d:%02d.%03d", h, m, s, ms)
              : std::sprintf(buf, "%02d:%02d:%02d", h, m, s);
  } else {
    len = include_milliseconds
              ? std::sprintf(buf, "%02d:%02d.%03d", m, s, ms)
              : std::sprintf(buf, "%02d:%02d", m, s);
  }

  return QString::fromLatin1(buf, len);
}

}  // namespace utils

void setSurfaceFormat() {
  QSurfaceFormat fmt;
#ifdef __APPLE__
  fmt.setVersion(3, 2);
  fmt.setProfile(QSurfaceFormat::OpenGLContextProfile::CoreProfile);
  fmt.setRenderableType(QSurfaceFormat::OpenGL);
#else
  fmt.setRenderableType(QSurfaceFormat::OpenGLES);
#endif
  fmt.setSamples(16);
  fmt.setStencilBufferSize(1);
  QSurfaceFormat::setDefaultFormat(fmt);
}

void sigTermHandler(int s) {
  std::signal(s, SIG_DFL);
  qApp->quit();
}

void initApp(int argc, char *argv[], bool disable_hidpi) {
  // setup signal handlers to exit gracefully
  std::signal(SIGINT, sigTermHandler);
  std::signal(SIGTERM, sigTermHandler);

  QString app_dir;
#ifdef __APPLE__
  // Get the devicePixelRatio, and scale accordingly to maintain 1:1 rendering
  QApplication tmp(argc, argv);
  app_dir = QCoreApplication::applicationDirPath();
  if (disable_hidpi) {
    qputenv("QT_SCALE_FACTOR", QString::number(1.0 / tmp.devicePixelRatio()).toLocal8Bit());
  }
#else
  app_dir = QFileInfo(util::readlink("/proc/self/exe").c_str()).path();
#endif

  qputenv("QT_DBL_CLICK_DIST", QByteArray::number(150));
  // ensure the current dir matches the exectuable's directory
  QDir::setCurrent(app_dir);

  setSurfaceFormat();
}
