#pragma once

#include <utility>

#include <QUndoCommand>
#include <QUndoStack>

#include "core/dbc/dbc_manager.h"
#include "core/streams/abstractstream.h"

class EditMsgCommand : public QUndoCommand {
public:
  EditMsgCommand(const MessageId &id, const QString &name, int size, const QString &node,
                 const QString &comment, QUndoCommand *parent = nullptr);
  void undo() override;
  void redo() override;

private:
  const MessageId id;
  QString old_name, new_name, old_comment, new_comment, old_node, new_node;
  int old_size = 0, new_size = 0;
};

class RemoveMsgCommand : public QUndoCommand {
public:
  RemoveMsgCommand(const MessageId &id, QUndoCommand *parent = nullptr);
  void undo() override;
  void redo() override;

private:
  const MessageId id;
  dbc::Msg message;
};

class AddSigCommand : public QUndoCommand {
public:
  AddSigCommand(const MessageId &id, const dbc::Signal &sig, QUndoCommand *parent = nullptr);
  void undo() override;
  void redo() override;

private:
  const MessageId id;
  bool msg_created = false;
  dbc::Signal signal = {};
};

class RemoveSigCommand : public QUndoCommand {
public:
  RemoveSigCommand(const MessageId &id, const dbc::Signal *sig, QUndoCommand *parent = nullptr);
  void undo() override;
  void redo() override;

private:
  const MessageId id;
  QList<dbc::Signal> sigs;
};

class EditSignalCommand : public QUndoCommand {
public:
  EditSignalCommand(const MessageId &id, const dbc::Signal *sig, const dbc::Signal &new_sig, QUndoCommand *parent = nullptr);
  void undo() override;
  void redo() override;

private:
  const MessageId id;
  QList<std::pair<dbc::Signal, dbc::Signal>> sigs; // QList<{old_sig, new_sig}>
};

namespace UndoStack {
  QUndoStack *instance();
  inline void push(QUndoCommand *cmd) { instance()->push(cmd); }
};
