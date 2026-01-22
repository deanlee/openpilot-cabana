#include <QApplication>
#include <QCommandLineParser>
#include <QProxyStyle>

#include "core/streams/device_stream.h"
#include "core/streams/panda_stream.h"
#include "core/streams/replay_stream.h"
#include "core/streams/socket_can_stream.h"
#include "mainwin.h"
#include "modules/settings/settings.h"
#include "utils/system_signal_handler.h"
#include "utils/util.h"

static AbstractStream* createStream(QCommandLineParser& p, QApplication* app) {
  if (p.isSet("msgq") || p.isSet("zmq"))
    return new DeviceStream(app, p.value("zmq"));

  if (p.isSet("panda") || p.isSet("panda-serial")) {
    try { return new PandaStream(app, {p.value("panda-serial")}); }
    catch (const std::exception& e) { qWarning() << e.what(); return nullptr; }
  }

  if (SocketCanStream::available() && p.isSet("socketcan"))
    return new SocketCanStream(app, {p.value("socketcan")});

  QString route = p.positionalArguments().value(0, p.isSet("demo") ? DEMO_ROUTE : "");
  if (!route.isEmpty()) {
    uint32_t flags = 0;
    if (p.isSet("ecam"))    flags |= REPLAY_FLAG_ECAM;
    if (p.isSet("qcam"))    flags |= REPLAY_FLAG_QCAMERA;
    if (p.isSet("dcam"))    flags |= REPLAY_FLAG_DCAM;
    if (p.isSet("no-vipc")) flags |= REPLAY_FLAG_NO_VIPC;

    auto replay = std::make_unique<ReplayStream>(app);
    if (replay->loadRoute(route, p.value("data_dir"), flags, p.isSet("auto")))
      return replay.release();
  }
  return nullptr;
}

class CabanaStyle : public QProxyStyle {
 public:
  CabanaStyle() : QProxyStyle() {
    x_icon = QIcon(utils::icon("x", QSize(16, 16)));
    maximize_icon = QIcon(utils::icon("maximize-2", QSize(16, 16)));
  }

  QIcon standardIcon(QStyle::StandardPixmap standardIcon, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override {
    if (standardIcon == SP_TitleBarCloseButton) {
      return x_icon;
    }
    if (standardIcon == SP_TitleBarNormalButton) {
      return maximize_icon;
    }
    return QProxyStyle::standardIcon(standardIcon, option, widget);
  }
  QIcon x_icon, maximize_icon;
};

int main(int argc, char* argv[]) {
  QCoreApplication::setApplicationName("Cabana");
  QCoreApplication::setApplicationVersion("1.1.0");
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

  initApp(argc, argv, false);
  QApplication app(argc, argv);
  app.setApplicationDisplayName(QString("Cabana v%1").arg(app.applicationVersion()));
  QPixmap appIcon = utils::icon("activity", QSize(64, 64), QColor(59, 130, 246));
  app.setWindowIcon(appIcon);
  app.setStyle(new CabanaStyle);

  SystemSignalHandler signal_handler;
  utils::setTheme(settings.theme);

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addPositionalArgument("route", "the drive to replay. find your drives at connect.comma.ai");
  parser.addOptions({
    {"demo", "use a demo route instead of providing your own"},
    {"auto", "Auto load the route from the best available source (no video): internal, openpilotci, comma_api, car_segments, testing_closet"},
    {"qcam", "load qcamera"},
    {"ecam", "load wide road camera"},
    {"dcam", "load driver camera"},
    {"msgq", "read can messages from the msgq"},
    {"panda", "read can messages from panda"},
    {{"panda-serial", "s"}, "read can messages from panda with given serial", "panda-serial"},
    {{"zmq", "z"}, "read can messages from zmq at the specified ip-address", "ip-address"},
    {{"data_dir", "d"}, "local directory with routes", "data_dir"},
    {"no-vipc", "do not output video"},
    {{"dbc", "b"}, "dbc file to open", "dbc"}
  });
  if (SocketCanStream::available())
    parser.addOption({"socketcan", "read can messages from given SocketCAN device", "socketcan"});

  parser.process(app);

  AbstractStream* stream = createStream(parser, &app);
  {
    MainWindow w(stream, parser.value("dbc"));
    app.exec();
  }
  return 0;
}
