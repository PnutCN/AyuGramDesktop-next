// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/mcp/mcp_remote.h"

#include "apiwrap.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_search_controller.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"

namespace AyuMcp {
namespace {

constexpr auto kPageSize = 100;
constexpr auto kMaxPages = 5;

struct FetchState {
	not_null<PeerData*> peer;
	MessageFilter filter;
	int limit = 0;
	int pages = 0;
	std::vector<MessageEntry> entries;
	Fn<bool()> alive;
	Fn<void(std::vector<MessageEntry>, QString)> done;
};

void requestPage(
	const std::shared_ptr<FetchState> &state,
	MsgId offsetId,
	TimeId offsetDate);

void pageReceived(
		const std::shared_ptr<FetchState> &state,
		const MTPmessages_Messages &result) {
	const auto parsed = Api::ParseHistoryResult(
		state->peer,
		MsgId(),
		Data::LoadDirection::Before,
		result);
	auto &owner = state->peer->owner();
	auto oldestId = MsgId();
	auto oldestDate = TimeId();
	for (const auto id : parsed.messageIds) {
		const auto item = owner.message(state->peer->id, id);
		if (!item) {
			continue;
		}
		auto entry = entryFromItem(item);
		if (!oldestId || id < oldestId) {
			oldestId = id;
			oldestDate = entry.date;
		}
		if (state->filter.matches(entry)) {
			state->entries.push_back(std::move(entry));
		}
	}
	++state->pages;
	if (!oldestId
		|| int(state->entries.size()) > state->limit
		|| state->pages >= kMaxPages
		|| (state->filter.since && oldestDate < state->filter.since)) {
		state->done(std::move(state->entries), QString());
	} else {
		requestPage(state, oldestId, 0);
	}
}

void requestPage(
		const std::shared_ptr<FetchState> &state,
		MsgId offsetId,
		TimeId offsetDate) {
	const auto peer = state->peer;
	const auto addOffset = 0;
	const auto maxId = 0;
	const auto minId = int(state->filter.afterId);
	const auto historyHash = uint64(0);
	peer->owner().histories().sendRequest(
		peer->owner().history(peer),
		Data::Histories::RequestType::History,
		[=](Fn<void()> finished) {
			return peer->session().api().request(MTPmessages_GetHistory(
				peer->input(),
				MTP_int(offsetId.bare),
				MTP_int(offsetDate),
				MTP_int(addOffset),
				MTP_int(kPageSize),
				MTP_int(maxId),
				MTP_int(minId),
				MTP_long(historyHash)
			)).done([=](const MTPmessages_Messages &result) {
				finished();
				if (state->alive()) {
					pageReceived(state, result);
				}
			}).fail([=](const MTP::Error &error) {
				finished();
				if (state->alive()) {
					state->done(std::move(state->entries), error.type());
				}
			}).handleFloodErrors().send();
		});
}

} // namespace

void fetchRemoteHistory(
		not_null<PeerData*> peer,
		const MessageFilter &filter,
		int limit,
		Fn<bool()> alive,
		Fn<void(std::vector<MessageEntry> entries, QString error)> done) {
	const auto state = std::make_shared<FetchState>(FetchState{
		.peer = peer,
		.filter = filter,
		.limit = limit,
		.alive = std::move(alive),
		.done = std::move(done),
	});
	requestPage(
		state,
		MsgId(filter.beforeId),
		filter.until ? (filter.until + 1) : TimeId());
}

} // namespace AyuMcp
