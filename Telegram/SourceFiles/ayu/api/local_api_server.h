// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <QtCore/QObject>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QTcpServer>

namespace AyuApi {

// Read-only HTTP endpoint bound to the loopback interface. It answers from
// the local cache and the deleted-message database only, and never issues a
// network request of its own.
class LocalServer final : public QObject
{
public:
	explicit LocalServer(QObject *parent = nullptr);

	bool start(quint16 port);
	void stop();
	[[nodiscard]] bool listening() const;
	[[nodiscard]] quint16 port() const;

private:
	void handleConnection();
	void respond(QTcpSocket *socket, const QByteArray &request);
	void sendMedia(QTcpSocket *socket, const QUrlQuery &query);

	QTcpServer _server;
};

// Starts or stops the server according to the current settings.
void applySettings();

[[nodiscard]] LocalServer *instance();

}
