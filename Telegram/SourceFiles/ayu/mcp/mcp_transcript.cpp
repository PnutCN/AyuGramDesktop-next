// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/mcp/mcp_transcript.h"

#include "ayu/utils/telegram_helpers.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "history/history_item.h"

#include <QtCore/QDateTime>

namespace AyuMcp {
namespace {

constexpr auto kSecondsPerHour = 3600;

[[nodiscard]] QString mediaLabel(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return QString();
	} else if (media->photo()) {
		return u"photo"_q;
	} else if (media->poll()) {
		return u"poll"_q;
	}
	const auto document = media->document();
	if (!document) {
		return u"media"_q;
	} else if (document->sticker()) {
		return u"sticker"_q;
	} else if (document->isVoiceMessage()) {
		return u"voice"_q;
	} else if (document->isVideoMessage()) {
		return u"video message"_q;
	} else if (document->isAnimation()) {
		return u"gif"_q;
	} else if (document->isVideoFile()) {
		return u"video"_q;
	} else if (document->isAudioFile()) {
		return u"audio"_q;
	}
	return u"file"_q;
}

[[nodiscard]] std::optional<TimeId> parseTime(
		const QJsonObject &arguments,
		const QString &name,
		QString *error) {
	const auto value = arguments.value(name).toString();
	if (value.isEmpty()) {
		return TimeId();
	}
	const auto parsed = QDateTime::fromString(value, Qt::ISODate);
	if (!parsed.isValid()) {
		*error = u"Argument '%1' must be an ISO 8601 date or date-time."_q.arg(
			name);
		return std::nullopt;
	}
	return TimeId(parsed.toSecsSinceEpoch());
}

} // namespace

QString formatTime(TimeId date) {
	return QDateTime::fromSecsSinceEpoch(date).toString(
		u"yyyy-MM-dd HH:mm"_q);
}

QString timezoneNote() {
	const auto offset = QDateTime::currentDateTime().offsetFromUtc();
	return u"Times are local, UTC%1%2."_q.arg(
		(offset < 0) ? u"-"_q : u"+"_q,
		QString::number(std::abs(offset) / double(kSecondsPerHour)));
}

bool MessageFilter::matches(const MessageEntry &entry) const {
	return (!beforeId || entry.id < beforeId)
		&& (!afterId || entry.id > afterId)
		&& (!since || entry.date >= since)
		&& (!until || entry.date <= until)
		&& (!fromId || entry.fromId == fromId)
		&& (query.isEmpty() || entry.text.contains(query, Qt::CaseInsensitive));
}

std::optional<MessageFilter> parseFilter(
		const QJsonObject &arguments,
		QString *error) {
	const auto since = parseTime(arguments, u"since"_q, error);
	const auto until = parseTime(arguments, u"until"_q, error);
	if (!since || !until) {
		return std::nullopt;
	}
	return MessageFilter{
		.beforeId = arguments.value(u"before_id"_q).toInteger(),
		.afterId = arguments.value(u"after_id"_q).toInteger(),
		.since = *since,
		.until = *until,
		.query = arguments.value(u"query"_q).toString(),
		.fromId = arguments.value(u"from_id"_q).toInteger(),
	};
}

MessageEntry entryFromItem(not_null<HistoryItem*> item) {
	const auto from = item->from();
	const auto service = item->isService();
	return {
		.id = item->id.bare,
		.date = item->date(),
		.fromId = getDialogIdFromPeer(from),
		.fromName = from->name(),
		.text = (service
			? item->notificationText().text
			: item->originalText().text),
		.replyTo = item->replyToId().bare,
		.media = mediaLabel(item),
		.service = service,
		.deleted = item->isDeleted(),
	};
}

MessagePage newest(std::vector<MessageEntry> entries, int limit) {
	ranges::stable_sort(entries, ranges::less(), &MessageEntry::id);
	entries.erase(
		ranges::unique(entries, ranges::equal_to(), &MessageEntry::id),
		end(entries));
	const auto more = (int(entries.size()) > limit);
	if (more) {
		entries.erase(begin(entries), end(entries) - limit);
	}
	return { .entries = std::move(entries), .more = more };
}

QString renderTranscript(const std::vector<MessageEntry> &entries) {
	auto lines = QStringList();
	for (const auto &entry : entries) {
		auto line = u"#%1 %2 %3"_q.arg(
			QString::number(entry.id),
			formatTime(entry.date),
			entry.fromName.isEmpty()
				? QString::number(entry.fromId)
				: entry.fromName);
		if (entry.deleted) {
			line += u" [deleted]"_q;
		}
		if (entry.service) {
			line += u" [service]"_q;
		}
		if (entry.replyTo) {
			line += u" [reply #%1]"_q.arg(entry.replyTo);
		}
		if (!entry.media.isEmpty()) {
			line += u" [%1]"_q.arg(entry.media);
		}
		auto text = entry.text;
		lines.append(line + u": "_q + text.replace('\n', u"\n  "_q));
	}
	return lines.join('\n');
}

QJsonArray entriesJson(const std::vector<MessageEntry> &entries) {
	auto result = QJsonArray();
	for (const auto &entry : entries) {
		auto json = QJsonObject{
			{ u"id"_q, entry.id },
			{ u"date"_q, entry.date },
			{
				u"from"_q,
				QJsonObject{
					{ u"id"_q, entry.fromId },
					{ u"name"_q, entry.fromName },
				},
			},
			{ u"text"_q, entry.text },
		};
		if (entry.replyTo) {
			json.insert(u"reply_to"_q, entry.replyTo);
		}
		if (!entry.media.isEmpty()) {
			json.insert(u"media"_q, entry.media);
		}
		if (entry.service) {
			json.insert(u"service"_q, true);
		}
		if (entry.deleted) {
			json.insert(u"deleted"_q, true);
		}
		result.append(json);
	}
	return result;
}

} // namespace AyuMcp
