// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

class HistoryItem;

namespace AyuMcp {

struct MessageEntry {
	int64 id = 0;
	TimeId date = 0;
	int64 fromId = 0;
	QString fromName;
	QString text;
	int64 replyTo = 0;
	QString media;
	bool service = false;
	bool deleted = false;
};

struct MessageFilter {
	int64 beforeId = 0;
	int64 afterId = 0;
	TimeId since = 0;
	TimeId until = 0;
	QString query;
	int64 fromId = 0;

	[[nodiscard]] bool matches(const MessageEntry &entry) const;
};

[[nodiscard]] std::optional<MessageFilter> parseFilter(
	const QJsonObject &arguments,
	QString *error);

[[nodiscard]] MessageEntry entryFromItem(not_null<HistoryItem*> item);

struct MessagePage {
	std::vector<MessageEntry> entries;
	bool more = false;
};

[[nodiscard]] MessagePage newest(
	std::vector<MessageEntry> entries,
	int limit);

[[nodiscard]] QString formatTime(TimeId date);
[[nodiscard]] QString timezoneNote();

[[nodiscard]] QString renderTranscript(
	const std::vector<MessageEntry> &entries);
[[nodiscard]] QJsonArray entriesJson(
	const std::vector<MessageEntry> &entries);

} // namespace AyuMcp
