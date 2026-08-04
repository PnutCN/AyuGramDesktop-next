// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/api/local_api_server.h"

#include "ayu/api/local_api_auth.h"
#include "ayu/ayu_settings.h"
#include "ayu/data/media_storage.h"
#include "ayu/data/messages_storage.h"
#include "ayu/utils/telegram_helpers.h"
#include "core/application.h"
#include "core/version.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMimeDatabase>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QTcpSocket>

namespace AyuApi {
namespace {

constexpr auto kDefaultLimit = 200;
constexpr auto kMaxLimit = 5000;

std::unique_ptr<LocalServer> GlobalServer;

enum Field : int {
	FieldBasic = 0x001,
	FieldMedia = 0x002,
	FieldEntities = 0x004,
	FieldForward = 0x008,
	FieldReply = 0x010,
	FieldReactions = 0x020,
	FieldViews = 0x040,
	FieldRawFlags = 0x080,
	FieldAll = 0x0FF,
};

enum class Filter {
	All,
	Deleted,
	Media,
	Service,
};

struct MessageQuery
{
	ID dialogId = 0;
	int limit = kDefaultLimit;
	ID beforeId = 0;
	ID afterId = 0;
	TimeId since = 0;
	TimeId until = 0;
	QString search;
	ID fromUser = 0;
	Filter filter = Filter::All;
	int fields = FieldBasic;
	bool resolveNames = false;
};

[[nodiscard]] Main::Session *activeSession() {
	if (!Core::IsAppLaunched() || !Core::App().domain().started()) {
		return nullptr;
	}
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (const auto session = account->maybeSession()) {
			return session;
		}
	}
	return nullptr;
}

[[nodiscard]] PeerData *resolvePeer(not_null<Main::Session*> session, ID dialogId) {
	auto &owner = session->data();
	const auto bare = BareId(std::abs(dialogId));
	if (dialogId < 0) {
		if (const auto peer = owner.peerLoaded(peerFromChannel(bare))) {
			return peer;
		}
		return owner.peerLoaded(peerFromChat(bare));
	}
	return owner.peerLoaded(peerFromUser(bare));
}

[[nodiscard]] QString peerTypeName(not_null<PeerData*> peer) {
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

// The signed dialog id keeps the peer type, unlike a bare id where a user and
// a channel can collide.
[[nodiscard]] QJsonValue senderJson(
		PeerData *peer,
		ID senderDialogId,
		bool resolveNames) {
	if (!resolveNames) {
		return QJsonValue(qint64(senderDialogId));
	}
	auto json = QJsonObject();
	json["id"] = qint64(senderDialogId);
	if (peer) {
		json["name"] = peer->name();
		const auto username = peer->username();
		json["username"] = username.isEmpty() ? QJsonValue() : QJsonValue(username);
		json["type"] = peerTypeName(peer);
	} else {
		// Peers that were never fully loaded have no name available locally.
		json["name"] = QJsonValue();
		json["username"] = QJsonValue();
		json["type"] = QJsonValue();
	}
	return json;
}

// Live messages already hold the sender object, so they never need a lookup.
[[nodiscard]] QJsonValue senderJson(
		not_null<PeerData*> peer,
		bool resolveNames) {
	return senderJson(peer.get(), getDialogIdFromPeer(peer), resolveNames);
}

// Stored messages only kept a bare id, so the peer has to be looked up and may
// legitimately be missing.
[[nodiscard]] QJsonValue storedSenderJson(
		Main::Session *session,
		ID bareId,
		bool resolveNames) {
	if (!resolveNames || !session) {
		return QJsonValue(qint64(bareId));
	}
	auto &owner = session->data();
	auto peer = owner.peerLoaded(peerFromUser(BareId(bareId)));
	if (!peer) {
		peer = owner.peerLoaded(peerFromChannel(BareId(bareId)));
	}
	return senderJson(peer, bareId, resolveNames);
}

[[nodiscard]] QString mediaTypeName(int documentType) {
	switch (documentType) {
	case AyuMedia::MediaPhoto: return u"photo"_q;
	case AyuMedia::MediaVideo: return u"video"_q;
	case AyuMedia::MediaVoice: return u"voice"_q;
	case AyuMedia::MediaAudio: return u"audio"_q;
	case AyuMedia::MediaSticker: return u"sticker"_q;
	case AyuMedia::MediaGif: return u"gif"_q;
	case AyuMedia::MediaFile: return u"file"_q;
	}
	return QString();
}

[[nodiscard]] QJsonValue mediaJson(
		int documentType,
		const std::string &path,
		const std::string &mime) {
	if (documentType == AyuMedia::MediaNone) {
		return QJsonValue();
	}
	auto json = QJsonObject();
	json["type"] = mediaTypeName(documentType);
	json["mime"] = QString::fromStdString(mime);
	json["cached"] = !path.empty();
	json["path"] = path.empty()
		? QJsonValue()
		: QJsonValue(QString::fromStdString(path));
	return json;
}

[[nodiscard]] QJsonObject itemJson(
		not_null<HistoryItem*> item,
		const MessageQuery &query,
		Main::Session *session) {
	auto json = QJsonObject();
	json["id"] = qint64(item->id.bare);
	json["date"] = qint64(item->date());
	json["from"] = senderJson(item->from(), query.resolveNames);
	json["text"] = item->originalText().text;
	json["deleted"] = item->isDeleted();
	json["out"] = item->out();
	json["source"] = u"live"_q;
	if (const auto reply = item->replyToId()) {
		json["reply_to"] = qint64(reply.bare);
	} else {
		json["reply_to"] = QJsonValue();
	}
	if (query.fields & FieldMedia) {
		// probeLocal, not trySaveLocal — listing must not write to disk.
		const auto media = AyuMedia::probeLocal(item);
		json["media"] = mediaJson(media.documentType, media.path, media.mimeType);
	}
	return json;
}

[[nodiscard]] QJsonObject storedJson(
		const AyuMessageBase &message,
		const MessageQuery &query,
		Main::Session *session) {
	auto json = QJsonObject();
	json["id"] = qint64(message.messageId);
	json["date"] = qint64(message.date);
	json["from"] = storedSenderJson(session, message.fromId, query.resolveNames);
	json["text"] = QString::fromStdString(message.text);
	json["deleted"] = true;
	json["source"] = u"stored"_q;
	json["deleted_at"] = qint64(message.entityCreateDate);
	// Reply and forward details are not persisted by AyuGram, so they cannot
	// be reported for a message that only exists in the database.
	json["reply_to"] = QJsonValue();
	if (query.fields & FieldMedia) {
		json["media"] = mediaJson(
			message.documentType,
			message.mediaPath,
			message.mimeType);
	}
	return json;
}

[[nodiscard]] bool matchesFilter(const QJsonObject &json, const MessageQuery &query) {
	switch (query.filter) {
	case Filter::Deleted:
		return json["deleted"].toBool();
	case Filter::Media:
		return !json["media"].isNull() && json.contains("media");
	case Filter::Service:
		return json["text"].toString().isEmpty();
	case Filter::All:
		break;
	}
	return true;
}

// The database cannot filter by time or sender, so those run here.
[[nodiscard]] bool matchesQuery(const QJsonObject &json, const MessageQuery &query) {
	const auto date = TimeId(json["date"].toInteger());
	if (query.since && date < query.since) {
		return false;
	}
	if (query.until && date > query.until) {
		return false;
	}
	if (query.fromUser) {
		const auto from = json["from"];
		const auto id = from.isObject()
			? ID(from.toObject()["id"].toInteger())
			: ID(from.toInteger());
		if (id != query.fromUser) {
			return false;
		}
	}
	if (!query.search.isEmpty()
		&& !json["text"].toString().contains(query.search, Qt::CaseInsensitive)) {
		return false;
	}
	return matchesFilter(json, query);
}

[[nodiscard]] int parseFields(const QString &value) {
	if (value.isEmpty()) {
		return FieldBasic;
	}
	auto result = int(FieldBasic);
	for (const auto &part : value.split(',', Qt::SkipEmptyParts)) {
		const auto name = part.trimmed().toLower();
		if (name == u"all"_q) {
			return FieldAll;
		} else if (name == u"media"_q) {
			result |= FieldMedia;
		} else if (name == u"entities"_q) {
			result |= FieldEntities;
		} else if (name == u"forward"_q) {
			result |= FieldForward;
		} else if (name == u"reply"_q) {
			result |= FieldReply;
		} else if (name == u"reactions"_q) {
			result |= FieldReactions;
		} else if (name == u"views"_q) {
			result |= FieldViews;
		} else if (name == u"raw_flags"_q) {
			result |= FieldRawFlags;
		}
	}
	return result;
}

[[nodiscard]] Filter parseFilter(const QString &value) {
	const auto name = value.trimmed().toLower();
	if (name == u"deleted"_q) {
		return Filter::Deleted;
	} else if (name == u"media"_q) {
		return Filter::Media;
	} else if (name == u"service"_q) {
		return Filter::Service;
	}
	return Filter::All;
}

[[nodiscard]] MessageQuery parseQuery(const QUrlQuery &url) {
	auto query = MessageQuery();
	query.dialogId = url.queryItemValue(u"peer"_q).toLongLong();
	const auto requested = url.queryItemValue(u"limit"_q).toInt();
	query.limit = std::clamp(
		requested > 0 ? requested : kDefaultLimit,
		1,
		kMaxLimit);
	query.beforeId = url.queryItemValue(u"before_id"_q).toLongLong();
	query.afterId = url.queryItemValue(u"after_id"_q).toLongLong();
	query.since = url.queryItemValue(u"since"_q).toInt();
	query.until = url.queryItemValue(u"until"_q).toInt();
	query.search = url.queryItemValue(u"q"_q);
	query.fromUser = url.queryItemValue(u"from"_q).toLongLong();
	query.filter = parseFilter(url.queryItemValue(u"filter"_q));
	query.fields = parseFields(url.queryItemValue(u"fields"_q));
	query.resolveNames = (url.queryItemValue(u"resolve_names"_q) == u"1"_q);
	return query;
}

[[nodiscard]] QJsonArray collectMessages(
		not_null<PeerData*> peer,
		const MessageQuery &query,
		Main::Session *session) {
	auto byId = base::flat_map<ID, QJsonObject>();

	if (query.filter != Filter::Deleted) {
		const auto history = peer->owner().history(peer);
		for (const auto &block : history->blocks) {
			for (const auto &view : block->messages) {
				const auto item = view->data();
				const auto id = ID(item->id.bare);
				if (query.beforeId && id >= query.beforeId) {
					continue;
				}
				if (query.afterId && id <= query.afterId) {
					continue;
				}
				auto json = itemJson(item, query, session);
				if (matchesQuery(json, query)) {
					byId.emplace(id, std::move(json));
				}
			}
		}
	}

	// The database only understands message-id bounds, everything else is
	// filtered after the rows come back.
	const auto stored = AyuMessages::getDeletedMessages(
		peer,
		0,
		query.afterId,
		query.beforeId,
		query.limit,
		query.search);
	for (const auto &message : stored) {
		const auto id = ID(message.messageId);
		if (byId.contains(id)) {
			continue;
		}
		auto json = storedJson(message, query, session);
		if (matchesQuery(json, query)) {
			byId.emplace(id, std::move(json));
		}
	}

	// Newest first, matching the ORDER BY messageId DESC the database uses.
	auto ordered = std::vector<QJsonObject>();
	ordered.reserve(byId.size());
	for (const auto &[id, json] : byId) {
		ordered.push_back(json);
	}
	std::reverse(ordered.begin(), ordered.end());
	if (int(ordered.size()) > query.limit) {
		ordered.resize(query.limit);
	}

	auto result = QJsonArray();
	for (const auto &json : ordered) {
		result.append(json);
	}
	return result;
}

[[nodiscard]] QJsonArray buildWarnings(const MessageQuery &query) {
	auto warnings = QJsonArray();
	warnings.append(u"only locally loaded messages are returned; open and scroll the chat to load more"_q);
	if (query.filter != Filter::Deleted) {
		warnings.append(u"stored (deleted) messages carry no reply or forward details, AyuGram does not persist them"_q);
	}
	if (query.since || query.until || query.fromUser) {
		warnings.append(u"time and sender filters are applied after the database query, so limit may cut results early"_q);
	}
	return warnings;
}

[[nodiscard]] QByteArray httpResponse(
		int code,
		const QByteArray &body,
		const QByteArray &contentType = "application/json; charset=utf-8") {
	const auto status = (code == 200)
		? "200 OK"
		: (code == 401) ? "401 Unauthorized"
		: (code == 403) ? "403 Forbidden"
		: (code == 404) ? "404 Not Found" : "400 Bad Request";
	return "HTTP/1.1 " + QByteArray(status) + "\r\n"
		"Content-Type: " + contentType + "\r\n"
		"Content-Length: " + QByteArray::number(body.size()) + "\r\n"
		"Connection: close\r\n\r\n" + body;
}

[[nodiscard]] QByteArray errorBody(const QString &message) {
	auto json = QJsonObject();
	json["error"] = message;
	return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

[[nodiscard]] QByteArray routeChats(const QUrlQuery &url) {
	const auto session = activeSession();
	if (!session) {
		return errorBody(u"no active session"_q);
	}
	const auto withCounts = (url.queryItemValue(u"with_counts"_q) == u"1"_q);

	auto chats = QJsonArray();
	for (const auto &row : session->data().chatsList()->indexed()->all()) {
		const auto peer = row->key().peer();
		if (!peer) {
			continue;
		}
		auto json = QJsonObject();
		json["id"] = qint64(getDialogIdFromPeer(peer));
		json["name"] = peer->name();
		json["type"] = peerTypeName(peer);
		const auto username = peer->username();
		if (!username.isEmpty()) {
			json["username"] = username;
		}
		if (withCounts) {
			json["has_deleted"] = AyuMessages::hasDeletedMessages(peer, 0);
		}
		chats.append(json);
	}

	auto json = QJsonObject();
	json["chats"] = chats;
	return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

[[nodiscard]] QByteArray routeMessages(const QUrlQuery &url) {
	const auto session = activeSession();
	if (!session) {
		return errorBody(u"no active session"_q);
	}
	const auto query = parseQuery(url);
	if (!query.dialogId) {
		return errorBody(u"peer is required"_q);
	}
	const auto peer = resolvePeer(session, query.dialogId);
	if (!peer) {
		return errorBody(u"peer not found locally"_q);
	}

	auto json = QJsonObject();
	json["peer"] = qint64(query.dialogId);
	json["name"] = peer->name();
	json["messages"] = collectMessages(peer, query, session);
	json["warnings"] = buildWarnings(query);
	return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

// Serving HTML or SVG from the API origin would let that markup read every
// other endpoint with the browser's credentials, so active types are forced
// to a download.
[[nodiscard]] QByteArray safeContentType(const QString &mime) {
	static const auto kActive = std::array{
		u"text/html"_q,
		u"application/xhtml+xml"_q,
		u"image/svg+xml"_q,
		u"text/xml"_q,
		u"application/xml"_q,
		u"application/javascript"_q,
		u"text/javascript"_q,
	};
	const auto lowered = mime.toLower();
	for (const auto &active : kActive) {
		if (lowered.startsWith(active)) {
			return "application/octet-stream";
		}
	}
	return mime.isEmpty()
		? QByteArray("application/octet-stream")
		: mime.toUtf8();
}

// The path comes from the database, so it is re-anchored under the media
// directory and canonicalised before anything is opened.
[[nodiscard]] QString resolveMediaPath(const QString &stored) {
	if (stored.isEmpty() || stored.contains(u".."_q)) {
		return QString();
	}
	const auto absolute = QDir(cWorkingDir()).absoluteFilePath(stored);
	const auto canonical = QFileInfo(absolute).canonicalFilePath();
	if (canonical.isEmpty()) {
		return QString();
	}
	const auto root = QFileInfo(AyuMedia::mediaDirectory()).canonicalFilePath();
	if (root.isEmpty() || !canonical.startsWith(root + u"/"_q)) {
		return QString();
	}
	return QFileInfo(canonical).isFile() ? canonical : QString();
}

struct MediaLookup
{
	QString path;
	QString mime;
	QString error;
};

[[nodiscard]] MediaLookup lookupMedia(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		ID messageId) {
	auto result = MediaLookup();

	for (const auto &message : AyuMessages::getDeletedMessages(
			peer,
			0,
			messageId - 1,
			messageId + 1,
			1)) {
		if (message.messageId != messageId) {
			continue;
		}
		result.path = resolveMediaPath(QString::fromStdString(message.mediaPath));
		result.mime = QString::fromStdString(message.mimeType);
		if (result.path.isEmpty()) {
			result.error = u"media was never cached locally"_q;
		}
		return result;
	}

	const auto item = session->data().message(peer->id, MsgId(messageId));
	if (!item) {
		result.error = u"message not found locally"_q;
		return result;
	}
	// Only here may the file be materialised, and only for a single message.
	const auto media = AyuMedia::trySaveLocal(item);
	result.mime = QString::fromStdString(media.mimeType);
	result.path = resolveMediaPath(QString::fromStdString(media.path));
	if (result.path.isEmpty()) {
		result.error = u"media was never cached locally"_q;
	}
	return result;
}

[[nodiscard]] QByteArray routeHealth() {
	const auto session = activeSession();
	auto json = QJsonObject();
	json["status"] = u"ok"_q;
	json["version"] = QString::fromLatin1(AppVersionStr);
	json["session"] = (session != nullptr);
	return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

} // namespace

LocalServer::LocalServer(QObject *parent)
: QObject(parent) {
	connect(&_server, &QTcpServer::newConnection, this, [=] {
		handleConnection();
	});
}

bool LocalServer::start(quint16 port) {
	stop();
	return _server.listen(QHostAddress::LocalHost, port);
}

void LocalServer::stop() {
	if (_server.isListening()) {
		_server.close();
	}
}

bool LocalServer::listening() const {
	return _server.isListening();
}

quint16 LocalServer::port() const {
	return _server.serverPort();
}

void LocalServer::handleConnection() {
	while (const auto socket = _server.nextPendingConnection()) {
		connect(socket, &QTcpSocket::readyRead, this, [=] {
			respond(socket, socket->readAll());
		});
		connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
	}
}

void LocalServer::respond(QTcpSocket *socket, const QByteArray &request) {
	const auto text = QString::fromUtf8(request);
	const auto lines = text.split(u"\r\n"_q);
	if (lines.isEmpty()) {
		socket->write(httpResponse(400, errorBody(u"malformed request"_q)));
		socket->disconnectFromHost();
		return;
	}
	const auto parts = lines.front().split(' ');
	if (parts.size() < 2 || parts[0] != u"GET"_q) {
		socket->write(httpResponse(400, errorBody(u"only GET is supported"_q)));
		socket->disconnectFromHost();
		return;
	}

	auto headers = base::flat_map<QString, QString>();
	for (auto i = 1; i != lines.size(); ++i) {
		const auto &line = lines[i];
		if (line.isEmpty()) {
			break;
		}
		const auto colon = line.indexOf(':');
		if (colon > 0) {
			headers.emplace(
				line.left(colon).trimmed().toLower(),
				line.mid(colon + 1).trimmed());
		}
	}
	const auto header = [&](const QString &name) {
		const auto i = headers.find(name);
		return (i != headers.end()) ? i->second : QString();
	};

	const auto url = QUrl(parts[1]);
	const auto path = url.path();
	const auto query = QUrlQuery(url);

	if (!originAllowed(header(u"origin"_q))) {
		socket->write(httpResponse(403, errorBody(u"cross-origin requests are refused"_q)));
		socket->disconnectFromHost();
		return;
	}
	if (!hostAllowed(header(u"host"_q), port())) {
		socket->write(httpResponse(403, errorBody(u"unexpected Host header"_q)));
		socket->disconnectFromHost();
		return;
	}
	const auto fetchSite = header(u"sec-fetch-site"_q);
	if (!fetchSite.isEmpty() && fetchSite != u"none"_q && fetchSite != u"same-origin"_q) {
		socket->write(httpResponse(403, errorBody(u"cross-site requests are refused"_q)));
		socket->disconnectFromHost();
		return;
	}

	if (path == u"/health"_q) {
		socket->write(httpResponse(200, routeHealth()));
		socket->disconnectFromHost();
		return;
	}

	const auto auth = header(u"authorization"_q).isEmpty()
		? header(u"x-ayu-token"_q)
		: header(u"authorization"_q);
	if (!authorized(auth, query.queryItemValue(u"token"_q))) {
		socket->write(httpResponse(401, errorBody(u"missing or invalid token"_q)));
		socket->disconnectFromHost();
		return;
	}

	if (path == u"/chats"_q) {
		socket->write(httpResponse(200, routeChats(query)));
	} else if (path == u"/messages"_q) {
		socket->write(httpResponse(200, routeMessages(query)));
	} else if (path == u"/media"_q) {
		sendMedia(socket, query);
		return;
	} else {
		socket->write(httpResponse(404, errorBody(u"unknown endpoint"_q)));
	}
	socket->disconnectFromHost();
}

void LocalServer::sendMedia(QTcpSocket *socket, const QUrlQuery &query) {
	const auto fail = [&](int code, const QString &message) {
		socket->write(httpResponse(code, errorBody(message)));
		socket->disconnectFromHost();
	};

	const auto session = activeSession();
	if (!session) {
		return fail(400, u"no active session"_q);
	}
	const auto dialogId = query.queryItemValue(u"peer"_q).toLongLong();
	const auto messageId = query.queryItemValue(u"msg_id"_q).toLongLong();
	if (!dialogId || !messageId) {
		return fail(400, u"peer and msg_id are required"_q);
	}
	const auto peer = resolvePeer(session, dialogId);
	if (!peer) {
		return fail(404, u"peer not found locally"_q);
	}

	const auto found = lookupMedia(session, peer, messageId);
	if (found.path.isEmpty()) {
		return fail(404, found.error.isEmpty()
			? u"media not available"_q
			: found.error);
	}

	auto file = QFile(found.path);
	if (!file.open(QIODevice::ReadOnly)) {
		return fail(404, u"media file is not readable"_q);
	}
	const auto mime = found.mime.isEmpty()
		? QMimeDatabase().mimeTypeForFile(found.path).name()
		: found.mime;
	const auto body = file.readAll();
	file.close();

	const auto name = QFileInfo(found.path).fileName().toUtf8().toPercentEncoding();
	auto response = QByteArray("HTTP/1.1 200 OK\r\n")
		+ "Content-Type: " + safeContentType(mime) + "\r\n"
		+ "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
		+ "Content-Disposition: attachment; filename*=UTF-8''" + name + "\r\n"
		+ "X-Content-Type-Options: nosniff\r\n"
		"Cache-Control: no-store\r\n"
		"Connection: close\r\n\r\n";
	socket->write(response);
	socket->write(body);
	socket->disconnectFromHost();
}

void applySettings() {
	const auto &settings = AyuSettings::getInstance();
	if (!GlobalServer) {
		GlobalServer = std::make_unique<LocalServer>();
	}
	if (!settings.localApiEnabled()) {
		GlobalServer->stop();
		return;
	}
	const auto port = quint16(settings.localApiPort());
	if (GlobalServer->listening() && GlobalServer->port() == port) {
		return;
	}
	if (!GlobalServer->start(port)) {
		LOG(("AyuGram API: failed to listen on port %1").arg(port));
	} else {
		LOG(("AyuGram API: listening on 127.0.0.1:%1").arg(port));
	}
}

LocalServer *instance() {
	return GlobalServer.get();
}

} // namespace AyuApi
