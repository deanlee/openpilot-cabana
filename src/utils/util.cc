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
#include <QWidget>
#include <QtSvg/QSvgRenderer>

#include "common/util.h"
#include "modules/settings/settings.h"

namespace utils {

static bool dark_theme = false;

bool isDarkTheme() { return dark_theme; }

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
  QColor icon_color = color.value_or(isDarkTheme() ? QColor("#d4d6e4") : QColor("#333333"));
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

  // Resolve automatic: follow system preference
  int resolved = theme;
  if (theme == 0) {
    resolved = style->standardPalette().color(QPalette::Window).lightness() < 128 ? DARK_THEME : LIGHT_THEME;
  }

  static int prev_resolved = 0;
  if (resolved == prev_resolved) return;
  prev_resolved = resolved;

  dark_theme = (resolved == DARK_THEME);
  QPalette new_palette;
  if (resolved == DARK_THEME) {
      // Modern dark theme inspired by VS Code / GitHub Dark
      // Uses blue-grey undertones instead of pure grey for a warmer, less fatiguing look.

      // ── Backgrounds ──────────────────────────────────────────────
      const QColor bg_base("#191a22");      // Deepest layer (main window)
      const QColor bg_surface("#1e1f28");   // Panels, message list, editors
      const QColor bg_elevated("#25262f");  // Cards, tooltips, dropdowns
      const QColor bg_hover("#2c2d3a");     // Hover / alternating rows

      // ── Foregrounds ──────────────────────────────────────────────
      const QColor fg_primary("#d4d6e4");    // Primary text (hex bytes, labels)
      const QColor fg_secondary("#8b8ea3");  // Muted text (timestamps, headers)
      const QColor fg_disabled("#4e5064");   // Disabled / suppressed

      // ── Accents ──────────────────────────────────────────────────
      const QColor accent("#7aa2f7");        // Selection, focus rings
      const QColor accent_text("#191a22");   // Text on accent background
      const QColor link_visited("#bb9af7");  // Visited links

      // ── Structure ────────────────────────────────────────────────
      const QColor border("#353647");  // Borders, separators
      const QColor shadow("#0c0d12");  // Drop shadows

      // ── Active / Normal ──────────────────────────────────────────
      new_palette.setColor(QPalette::Window, bg_base);
      new_palette.setColor(QPalette::WindowText, fg_primary);
      new_palette.setColor(QPalette::Base, bg_surface);
      new_palette.setColor(QPalette::AlternateBase, bg_hover);
      new_palette.setColor(QPalette::Text, fg_primary);
      new_palette.setColor(QPalette::Button, bg_surface);
      new_palette.setColor(QPalette::ButtonText, fg_primary);
      new_palette.setColor(QPalette::BrightText, QColor("#f0f0f0"));
      new_palette.setColor(QPalette::PlaceholderText, fg_secondary);

      new_palette.setColor(QPalette::Highlight, accent);
      new_palette.setColor(QPalette::HighlightedText, accent_text);

      new_palette.setColor(QPalette::ToolTipBase, bg_elevated);
      new_palette.setColor(QPalette::ToolTipText, fg_primary);

      new_palette.setColor(QPalette::Link, accent);
      new_palette.setColor(QPalette::LinkVisited, link_visited);

      // ── 3D frame / depth ─────────────────────────────────────────
      new_palette.setColor(QPalette::Light, border);
      new_palette.setColor(QPalette::Midlight, QColor("#2f3040"));
      new_palette.setColor(QPalette::Mid, QColor("#3d3e52"));
      new_palette.setColor(QPalette::Dark, QColor("#111218"));
      new_palette.setColor(QPalette::Shadow, shadow);

      // ── Disabled state ────────────────────────────────────────────
      new_palette.setColor(QPalette::Disabled, QPalette::WindowText, fg_disabled);
      new_palette.setColor(QPalette::Disabled, QPalette::Text, fg_disabled);
      new_palette.setColor(QPalette::Disabled, QPalette::ButtonText, fg_disabled);
      new_palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#353647"));
      new_palette.setColor(QPalette::Disabled, QPalette::HighlightedText, fg_disabled);
      new_palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, fg_disabled);

    } else {
      new_palette = style->standardPalette();
    }
    qApp->setPalette(new_palette);
    style->polish(qApp);
    for (auto w : QApplication::allWidgets()) {
      w->setPalette(new_palette);
    }
}

QString formatSeconds(double sec, bool include_milliseconds, bool absolute_time) {
  if (absolute_time) {
    return QDateTime::fromMSecsSinceEpoch(sec * 1000)
        .toString(include_milliseconds ? "yyyy-MM-dd HH:mm:ss.zzz" : "yyyy-MM-dd HH:mm:ss");
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
    len = include_milliseconds ? std::sprintf(buf, "%02d:%02d:%02d.%03d", h, m, s, ms)
                               : std::sprintf(buf, "%02d:%02d:%02d", h, m, s);
  } else {
    len = include_milliseconds ? std::sprintf(buf, "%02d:%02d.%03d", m, s, ms) : std::sprintf(buf, "%02d:%02d", m, s);
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

void initApp(int argc, char* argv[], bool disable_hidpi) {
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
