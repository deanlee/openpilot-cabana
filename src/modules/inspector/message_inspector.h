#pragma once

#include <QStackedWidget>
#include "message_view.h"
#include "welcome_widget.h"

class ChartsPanel;
class MessageInspector : public QStackedWidget {
  Q_OBJECT
public:
  MessageInspector(ChartsPanel *charts, QWidget *parent);
  void setMessage(const MessageId &message_id);
  MessageView* getMessageView() { return message_view; }
  void clear();

private:
  MessageView *message_view = nullptr;
  WelcomeWidget *welcome_widget = nullptr;
};
