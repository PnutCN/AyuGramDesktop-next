// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/api/local_api_server.h"

#include "ayu/ayu_settings.h"
#include "ayu/data/media_storage.h"
#include "ayu/data/messages_storage.h"
#include "ayu/utils/telegram_helpers.h"
#include "core/application.h"
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

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QTcpSocket>

namespace AyuApi {
namespace {

constexpr auto kDefaultLimit = 200;
constexpr auto kMaxLimit = 5000;

std::unique_ptr<LocalServer> GlobalServer;

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

[[nodiscard]] QJsonObject mediaJson(int documentType, const std::string &path, const std::string &mime) {
	if (documentType == AyuMedia::MediaNone) {
		return QJsonObject();
	}
	auto json = QJsonObject();
	json["type"] = mediaTypeName(documentType);
	json["mime"] = QString::fromStdString(mime);
	if (!path.empty()) {
		json["path"] = QString::fromStdString(path);
	}
	return json;
}

[[nodiscard]] QJsonObject itemJson(not_null<HistoryItem*> item) {
	auto json = QJsonObject();
	json["id"] = qint64(item->id.bare);
	json["date"] = qint64(item->date());
	json["from"] = qint64(item->from()->id.value & PeerId::kChatTypeMask);
	json["text"] = item->originalText().text;
	json["deleted"] = item->isDeleted();
	json["out"] = item->out();
	if (const auto reply = item->replyToId()) {
		json["reply_to"] = qint64(reply.bare);
	}
	const auto media = AyuMedia::trySaveLocal(item);
	const auto mediaObject = mediaJson(media.documentType, media.path, media.mimeType);
	if (!mediaObject.isEmpty()) {
		json["media"] = mediaObject;
	}
	return json;
}

[[nodiscard]] QJsonObject storedJson(const AyuMessageBase &message) {
	auto json = QJsonObject();
	json["id"] = qint64(message.messageId);
	json["date"] = qint64(message.date);
	json["from"] = qint64(message.fromId);
	json["text"] = QString::fromStdString(message.text);
	json["deleted"] = true;
	const auto mediaObject = mediaJson(
		message.documentType,
		message.mediaPath,
		message.mimeType);
	if (!mediaObject.isEmpty()) {
		json["media"] = mediaObject;
	}
	return json;
}

[[nodiscard]] QJsonArray collectMessages(not_null<PeerData*> peer, int limit) {
	auto byId = base::flat_map<qint64, QJsonObject>();

	const auto history = peer->owner().history(peer);
	for (const auto &block : history->blocks) {
		for (const auto &view : block->messages) {
			const auto item = view->data();
			byId.emplace(qint64(item->id.bare), itemJson(item));
		}
	}

	const auto stored = AyuMessages::getDeletedMessages(peer, 0, 0, 0, limit);
	for (const auto &message : stored) {
		byId.emplace(qint64(message.messageId), storedJson(message));
	}

	auto result = QJsonArray();
	for (const auto &[id, json] : byId) {
		result.append(json);
	}
	while (result.size() > limit) {
		result.removeFirst();
	}
	return result;
}

[[nodiscard]] QJsonArray collectChats(not_null<Main::Session*> session) {
	auto result = QJsonArray();
	for (const auto &row : session->data().chatsList()->indexed()->all()) {
		const auto peer = row->key().peer();
		if (!peer) {
			continue;
		}
		auto json = QJsonObject();
		json["id"] = qint64(getDialogIdFromPeer(peer));
		json["name"] = peer->name();
		json["type"] = peer->isChannel()
			? (peer->isBroadcast() ? u"channel"_q : u"supergroup"_q)
			: peer->isChat()
			? u"group"_q
			: u"user"_q;
		result.append(json);
	}
	return result;
}

[[nodiscard]] QByteArray httpResponse(int code, const QByteArray &body) {
	const auto status = (code == 200)
		? "200 OK"
		: (code == 404) ? "404 Not Found" : "400 Bad Request";
	return "HTTP/1.1 " + QByteArray(status) + "\r\n"
		"Content-Type: application/json; charset=utf-8\r\n"
		"Content-Length: " + QByteArray::number(body.size()) + "\r\n"
		"Connection: close\r\n\r\n" + body;
}

[[nodiscard]] QByteArray errorBody(const QString &message) {
	auto json = QJsonObject();
	json["error"] = message;
	return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

[[nodiscard]] QByteArray routeChats() {
	const auto session = activeSession();
	if (!session) {
		return errorBody(u"no active session"_q);
	}
	auto json = QJsonObject();
	json["chats"] = collectChats(session);
	return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

[[nodiscard]] QByteArray routeMessages(const QUrlQuery &query) {
	const auto session = activeSession();
	if (!session) {
		return errorBody(u"no active session"_q);
	}
	auto ok = false;
	const auto dialogId = query.queryItemValue(u"peer"_q).toLongLong(&ok);
	if (!ok) {
		return errorBody(u"peer is required"_q);
	}
	const auto peer = resolvePeer(session, dialogId);
	if (!peer) {
		return errorBody(u"peer not found locally"_q);
	}
	const auto requested = query.queryItemValue(u"limit"_q).toInt();
	const auto limit = std::clamp(
		requested > 0 ? requested : kDefaultLimit,
		1,
		kMaxLimit);

	auto json = QJsonObject();
	json["peer"] = qint64(dialogId);
	json["name"] = peer->name();
	json["messages"] = collectMessages(peer, limit);
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
	const auto head = QString::fromUtf8(request.left(request.indexOf('\r')));
	const auto parts = head.split(' ');
	if (parts.size() < 2 || parts[0] != u"GET"_q) {
		socket->write(httpResponse(400, errorBody(u"only GET is supported"_q)));
		socket->disconnectFromHost();
		return;
	}
	const auto url = QUrl(parts[1]);
	const auto path = url.path();
	const auto query = QUrlQuery(url);

	if (path == u"/chats"_q) {
		socket->write(httpResponse(200, routeChats()));
	} else if (path == u"/messages"_q) {
		socket->write(httpResponse(200, routeMessages(query)));
	} else {
		socket->write(httpResponse(404, errorBody(u"unknown endpoint"_q)));
	}
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
