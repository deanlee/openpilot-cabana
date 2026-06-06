#include "device_stream.h"

#include <QThread>
#include <QDebug>

#include <cstdint>
#include <cstring>
#include <vector>

#include "cereal/services.h"
#include "cereal/messaging/impl_zmq.h"

DeviceStream::DeviceStream(QObject* parent, QString address) : LiveStream(parent), zmq_address(address) {}

void DeviceStream::streamThread() {
  zmq_address.isEmpty() ? unsetenv("ZMQ") : setenv("ZMQ", "1", 1);

  std::unique_ptr<Context> context;
  std::string address = zmq_address.isEmpty() ? "127.0.0.1" : zmq_address.toStdString();
  std::unique_ptr<SubSocket> sock;

  if (zmq_address.isEmpty()) {
    context.reset(Context::create());
    sock.reset(SubSocket::create(context.get(), "can", address, false, true, services.at("can").queue_size));
  } else {
    context.reset(new ZMQContext());
    sock.reset(new ZMQSubSocket());
    sock->connect(context.get(), "can", address, false, true, services.at("can").queue_size);
  }
  if (!sock) {
    qWarning() << "DeviceStream failed to create subscription socket";
    return;
  }

  // run as fast as messages come in
  while (!QThread::currentThread()->isInterruptionRequested()) {
    std::unique_ptr<Message> msg(sock->receive(true));
    if (!msg) {
      QThread::msleep(50);
      continue;
    }

    const size_t size = msg->getSize();
    if (size % sizeof(capnp::word) != 0) {
      qWarning() << "DeviceStream received malformed capnp payload of size" << size;
      continue;
    }

    const auto* data = reinterpret_cast<const uint8_t*>(msg->getData());
    const size_t word_count = size / sizeof(capnp::word);

    // capnp words require alignment. Copy only when incoming payload is unaligned.
    if ((reinterpret_cast<uintptr_t>(data) % alignof(capnp::word)) == 0) {
      handleEvent(kj::ArrayPtr<capnp::word>(reinterpret_cast<capnp::word*>(msg->getData()), word_count));
    } else {
      std::vector<capnp::word> aligned_words(word_count);
      std::memcpy(aligned_words.data(), data, size);
      handleEvent(kj::ArrayPtr<capnp::word>(aligned_words.data(), word_count));
    }
  }
}
