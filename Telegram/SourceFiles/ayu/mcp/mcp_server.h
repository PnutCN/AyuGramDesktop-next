// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <QtNetwork/QTcpServer>

class QTcpSocket;

namespace AyuMcp {

inline constexpr auto kMinPort = 1024;
inline constexpr auto kMaxPort = 65535;

struct HttpRequest;

class Server final : public QObject {
public:
	Server();

	[[nodiscard]] bool start(quint16 port);
	[[nodiscard]] quint16 port() const;

private:
	void accept();
	void handle(not_null<QTcpSocket*> socket, const HttpRequest &request);

	QTcpServer _server;

};

void applySettings(bool interactive = false);

[[nodiscard]] QString clientConfig();

} // namespace AyuMcp
