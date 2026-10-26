// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/mcp/tools/mcp_tools.h"

#include "ayu/mcp/mcp_context.h"
#include "ayu/mcp/mcp_registry.h"
#include "ayu/mcp/mcp_remote.h"
#include "ayu/mcp/mcp_transcript.h"
#include "ayu/data/ayu_database.h"
#include "ayu/utils/telegram_helpers.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"


namespace AyuMcp {
namespace {

constexpr auto kDefaultLimit = 50;
constexpr auto kScanBatch = 500;
constexpr auto kMaxScanned = 20000;

[[nodiscard]] std::vector<MessageEntry> loadedEntries(
		not_null<PeerData*> peer,
		const MessageFilter &filter) {
	auto result = std::vector<MessageEntry>();
	const auto history = peer->owner().historyLoaded(peer);
	if (!history) {
		return result;
	}
	for (const auto &block : history->blocks) {
		for (const auto &view : block->messages) {
			auto entry = entryFromItem(view->data());
			if (filter.matches(entry)) {
				result.push_back(std::move(entry));
			}
		}
	}
	return result;
}

[[nodiscard]] PeerData *storedSender(
		not_null<Main::Session*> session,
		ID bareId) {
	auto &owner = session->data();
	if (const auto user = owner.peerLoaded(peerFromUser(BareId(bareId)))) {
		return user;
	}
	return owner.peerLoaded(peerFromChannel(BareId(bareId)));
}

[[nodiscard]] MessageEntry entryFromStored(
		not_null<Main::Session*> session,
		const DeletedMessage &message) {
	const auto sender = storedSender(session, message.fromId);
	return {
		.id = message.messageId,
		.date = message.date,
		.fromId = sender ? getDialogIdFromPeer(sender) : message.fromId,
		.fromName = sender ? sender->name() : QString(),
		.text = QString::fromStdString(message.text),
		.deleted = true,
	};
}

// WHY: the database bounds by id only, so the time range is applied here,
// walking batches back until the limit or the start of the range is hit.
[[nodiscard]] std::vector<DeletedMessage> scanDeleted(
		ID userId,
		ID dialogId,
		const MessageFilter &filter,
		int limit) {
	auto result = std::vector<DeletedMessage>();
	const auto query = filter.query.toStdString();
	const auto fromId = std::abs(filter.fromId);
	auto cursor = filter.beforeId;
	auto scanned = 0;
	while (int(result.size()) <= limit && scanned < kMaxScanned) {
		const auto rows = AyuDatabase::getDeletedMessages(
			userId,
			dialogId,
			0,
			filter.afterId,
			cursor,
			kScanBatch,
			query);
		for (const auto &row : rows) {
			if ((!filter.since || row.date >= filter.since)
				&& (!filter.until || row.date <= filter.until)
				&& (!fromId || row.fromId == fromId)) {
				result.push_back(row);
			}
		}
		scanned += int(rows.size());
		if (int(rows.size()) < kScanBatch
			|| (filter.since && rows.back().date < filter.since)) {
			break;
		}
		cursor = rows.back().messageId;
	}
	return result;
}

void addDeleted(
		const CallContext &context,
		not_null<PeerData*> peer,
		const MessageFilter &filter,
		int limit,
		std::vector<MessageEntry> entries,
		Fn<void(std::vector<MessageEntry>)> done) {
	const auto session = context.session;
	const auto userId = ID(session->userId().bare & PeerId::kChatTypeMask);
	const auto dialogId = getDialogIdFromPeer(peer);
	const auto alive = context.alive;
	crl::async([=] {
		auto rows = scanDeleted(userId, dialogId, filter, limit);
		crl::on_main(session, [=, rows = std::move(rows)] {
			if (!alive()) {
				return;
			}
			auto all = entries;
			for (const auto &row : rows) {
				all.push_back(entryFromStored(session, row));
			}
			done(std::move(all));
		});
	});
}

void respond(
		not_null<PeerData*> peer,
		int64 chatId,
		int limit,
		std::vector<MessageEntry> matched,
		const QString &note,
		const Fn<void(ToolResult)> &done) {
	const auto page = newest(std::move(matched), limit);
	const auto &entries = page.entries;
	auto text = u"%1 (%2). %3%4\n\n"_q.arg(
		peer->name(),
		QString::number(chatId),
		timezoneNote(),
		note.isEmpty() ? QString() : (' ' + note));
	auto structured = QJsonObject{
		{
			u"chat"_q,
			QJsonObject{
				{ u"id"_q, chatId },
				{ u"title"_q, peer->name() },
			},
		},
		{ u"messages"_q, entriesJson(entries) },
	};
	if (entries.empty()) {
		text += u"No messages matched. Opening the chat in AyuGram "
			"loads more of its history."_q;
	} else {
		text += renderTranscript(entries);
	}
	if (page.more) {
		const auto next = entries.front().id;
		text += u"\n\nMore messages match, call again with before_id=%1."_q.arg(
			next);
		structured.insert(u"next_before_id"_q, next);
	}
	done({ .text = text, .structured = structured });
}

void getMessages(
		const CallContext &context,
		const QJsonObject &arguments,
		Fn<void(ToolResult)> done) {
	const auto chatId = arguments.value(u"chat_id"_q).toInteger();
	const auto peer = resolveChat(context.session, chatId);
	if (!peer) {
		done(toolError(u"Chat %1 is not loaded yet, list_chats shows "
			"the available ones."_q.arg(chatId)));
		return;
	}
	auto error = QString();
	const auto filter = parseFilter(arguments, &error);
	if (!filter) {
		done(toolError(error));
		return;
	}
	const auto limit = arguments.value(u"limit"_q).toInt(kDefaultLimit);
	const auto include = arguments.value(u"include"_q).toString();
	const auto deletedOnly = (include == u"deleted_only"_q);
	const auto withDeleted = (include != u"exclude_deleted"_q);
	auto loaded = deletedOnly
		? std::vector<MessageEntry>()
		: loadedEntries(peer, *filter);
	const auto finish = [=](std::vector<MessageEntry> entries, QString note) {
		if (!withDeleted) {
			entries.erase(
				ranges::remove(entries, true, &MessageEntry::deleted),
				end(entries));
			respond(peer, chatId, limit, std::move(entries), note, done);
			return;
		}
		addDeleted(context, peer, *filter, limit, std::move(entries), [=](
				std::vector<MessageEntry> all) {
			respond(peer, chatId, limit, std::move(all), note, done);
		});
	};
	if (deletedOnly || !tierEnabled(Tier::Network)) {
		finish(std::move(loaded), QString());
		return;
	}
	fetchRemoteHistory(peer, *filter, limit, context.alive, [=](
			std::vector<MessageEntry> remote,
			QString error) {
		auto entries = loaded;
		entries.insert(end(entries), begin(remote), end(remote));
		finish(std::move(entries), error.isEmpty()
			? u"Includes history fetched from Telegram."_q
			: u"Fetching from Telegram failed (%1), showing local "
				"messages only."_q.arg(error));
	});
}

} // namespace

void registerMessageTools() {
	registerTool({
		.name = u"get_messages"_q,
		.title = u"Get messages"_q,
		.description = u"Reads messages of a chat that this client has "
			"already loaded, plus messages AyuGram saved after they were "
			"deleted, oldest first. When the user allowed it in AyuGram "
			"settings, missing history is fetched from Telegram as well, "
			"without marking anything as read. Chat ids come from "
			"list_chats. All filters combine."_q,
		.inputSchema = parseSchema(R"({
			"type": "object",
			"properties": {
				"chat_id": {
					"type": "integer",
					"description": "Chat id from list_chats."
				},
				"limit": {
					"type": "integer",
					"minimum": 1,
					"maximum": 500,
					"default": 50
				},
				"before_id": {
					"type": "integer",
					"minimum": 1,
					"description": "Only messages with a smaller id, for paging back."
				},
				"after_id": {
					"type": "integer",
					"minimum": 1,
					"description": "Only messages with a larger id."
				},
				"since": {
					"type": "string",
					"description": "ISO 8601 date or date-time, local time unless an offset is given."
				},
				"until": {
					"type": "string",
					"description": "ISO 8601 date or date-time, local time unless an offset is given."
				},
				"query": {
					"type": "string",
					"description": "Case-insensitive text the message must contain."
				},
				"from_id": {
					"type": "integer",
					"description": "Only messages from this sender id."
				},
				"include": {
					"type": "string",
					"enum": ["all", "deleted_only", "exclude_deleted"],
					"default": "all",
					"description": "Deleted messages keep no reply or media details."
				}
			},
			"required": ["chat_id"],
			"additionalProperties": false
		})"),
		.handler = getMessages,
	});
}

} // namespace AyuMcp
