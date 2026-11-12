// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/mcp/mcp_context.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_session.h"

namespace AyuMcp {

PeerData *resolveChat(not_null<Main::Session*> session, int64 chatId) {
	auto &owner = session->data();
	const auto bare = BareId(std::abs(chatId));
	if (chatId > 0) {
		return owner.peerLoaded(peerFromUser(bare));
	} else if (const auto channel = owner.peerLoaded(peerFromChannel(bare))) {
		return channel;
	}
	return owner.peerLoaded(peerFromChat(bare));
}

QString peerTypeName(not_null<PeerData*> peer) {
	if (peer->isBroadcast()) {
		return u"channel"_q;
	} else if (peer->isChannel()) {
		return u"supergroup"_q;
	} else if (peer->isChat()) {
		return u"group"_q;
	} else if (peer->isBot()) {
		return u"bot"_q;
	}
	return u"user"_q;
}

} // namespace AyuMcp
