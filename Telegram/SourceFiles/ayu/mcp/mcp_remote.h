// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/mcp/mcp_transcript.h"

class PeerData;

namespace AyuMcp {

void fetchRemoteHistory(
	not_null<PeerData*> peer,
	const MessageFilter &filter,
	int limit,
	Fn<bool()> alive,
	Fn<void(std::vector<MessageEntry> entries, QString error)> done);

} // namespace AyuMcp
