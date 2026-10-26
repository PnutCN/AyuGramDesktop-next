// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/mcp/tools/mcp_tools.h"

#include "ayu/data/ayu_database.h"
#include "ayu/mcp/mcp_context.h"
#include "ayu/mcp/mcp_registry.h"
#include "ayu/mcp/mcp_transcript.h"
#include "ayu/utils/telegram_helpers.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace AyuMcp {
namespace {

constexpr auto kMaxVersions = 100;

void getEditHistory(
		const CallContext &context,
		const QJsonObject &arguments,
		Fn<void(ToolResult)> done) {
	const auto chatId = arguments.value(u"chat_id"_q).toInteger();
	const auto messageId = arguments.value(u"message_id"_q).toInteger();
	const auto peer = resolveChat(context.session, chatId);
	if (!peer) {
		done(toolError(u"Chat %1 is not loaded yet, list_chats shows "
			"the available ones."_q.arg(chatId)));
		return;
	}
	const auto session = context.session;
	auto versions = AyuDatabase::getEditedMessages(
		ID(session->userId().bare & PeerId::kChatTypeMask),
		getDialogIdFromPeer(peer),
		messageId,
		0,
		0,
		kMaxVersions);
	ranges::reverse(versions);

	auto lines = QStringList{
		u"Edit history of #%1 in %2, oldest first. %3"_q.arg(
			QString::number(messageId),
			peer->name(),
			timezoneNote()),
	};
	auto list = QJsonArray();
	for (const auto &version : versions) {
		const auto text = QString::fromStdString(version.text);
		lines.append(u"Until %1: %2"_q.arg(
			formatTime(version.entityCreateDate),
			QString(text).replace('\n', u"\n  "_q)));
		list.append(QJsonObject{
			{ u"text"_q, text },
			{ u"replaced_at"_q, version.entityCreateDate },
		});
	}
	auto structured = QJsonObject{
		{ u"chat_id"_q, chatId },
		{ u"message_id"_q, messageId },
		{ u"versions"_q, list },
	};
	const auto item = session->data().message(peer->id, MsgId(messageId));
	if (item) {
		const auto current = item->originalText().text;
		lines.append(u"Now: "_q + current);
		structured.insert(u"current"_q, current);
	}
	if (versions.empty()) {
		lines.append(u"AyuGram saved no earlier versions of this message."_q);
	}
	done({ .text = lines.join('\n'), .structured = structured });
}

} // namespace

void registerHistoryTools() {
	registerTool({
		.name = u"get_edit_history"_q,
		.title = u"Get edit history"_q,
		.description = u"Lists earlier versions of a message that AyuGram "
			"saved when it was edited. Message ids come from "
			"get_messages."_q,
		.inputSchema = parseSchema(R"({
			"type": "object",
			"properties": {
				"chat_id": {
					"type": "integer",
					"description": "Chat id from list_chats."
				},
				"message_id": {
					"type": "integer",
					"minimum": 1
				}
			},
			"required": ["chat_id", "message_id"],
			"additionalProperties": false
		})"),
		.handler = getEditHistory,
	});
}

} // namespace AyuMcp
