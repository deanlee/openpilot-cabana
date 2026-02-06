#include "message_inspector.h"

#include "mainwin.h"
#include "modules/charts/charts_panel.h"

MessageInspector::MessageInspector(ChartsPanel* charts, QWidget* parent) : QStackedWidget(parent) {
  addWidget(welcome_widget = new WelcomeWidget(this));
  addWidget(message_view = new MessageView(charts, this));
}

void MessageInspector::setMessage(const MessageId& message_id) {
  if (currentWidget() != message_view) {
    setCurrentWidget(message_view);
  }
  message_view->setMessage(message_id);
}

void MessageInspector::clear() {
  message_view->resetState();
  if (currentWidget() != welcome_widget) {
    setCurrentWidget(welcome_widget);
  }
}
