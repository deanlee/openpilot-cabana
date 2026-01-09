#pragma once

#include <optional>

#include "core/dbc/dbc_manager.h"

void exportMessagesToCSV(const QString &file_name, std::optional<MessageId> msg_id = std::nullopt);
void exportSignalsToCSV(const QString &file_name, const MessageId &msg_id);
