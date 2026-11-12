// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace AyuMcp {

[[nodiscard]] PeerData *resolveChat(
	not_null<Main::Session*> session,
	int64 chatId);
[[nodiscard]] QString peerTypeName(not_null<PeerData*> peer);

} // namespace AyuMcp
