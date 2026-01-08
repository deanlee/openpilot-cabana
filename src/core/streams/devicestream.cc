#include "devicestream.h"

#include "cereal/services.h"

#include <QThread>

DeviceStream::DeviceStream(QObject *parent, QString address) : zmq_address(address), LiveStream(parent) {
}

void DeviceStream::streamThread() {
  zmq_address.isEmpty() ? unsetenv("ZMQ") : setenv("ZMQ", "1", 1);

  std::unique_ptr<Context> context(Context::create());
  std::string address = zmq_address.isEmpty() ? "127.0.0.1" : zmq_address.toStdString();
  std::unique_ptr<SubSocket> sock(SubSocket::create(context.get(), "can", address, false, true, services.at("can").queue_size));
  assert(sock != NULL);
  // run as fast as messages come in
  while (!QThread::currentThread()->isInterruptionRequested()) {
    std::unique_ptr<Message> msg(sock->receive(true));
    if (!msg) {
      QThread::msleep(50);
      continue;
    }
    handleEvent(kj::ArrayPtr<capnp::word>((capnp::word*)msg->getData(), msg->getSize() / sizeof(capnp::word)));
  }
}
