// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/api/local_api_auth.h"

#include "settings.h"

#include <QtCore/QFile>
#include <QtCore/QRandomGenerator>

namespace AyuApi {
namespace {

constexpr auto kTokenParts = 4;

QString GlobalToken;

[[nodiscard]] QString tokenPath() {
	return cWorkingDir() + u"tdata/ayu_api_token"_q;
}

[[nodiscard]] QString generate() {
	auto bytes = QByteArray();
	for (auto i = 0; i != kTokenParts; ++i) {
		const auto value = QRandomGenerator::system()->generate64();
		bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
	}
	return u"ayu_"_q + QString::fromLatin1(
		bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

void store(const QString &value) {
	auto file = QFile(tokenPath());
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return;
	}
	file.write(value.toUtf8());
	file.close();
	file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

[[nodiscard]] QString load() {
	auto file = QFile(tokenPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	const auto value = QString::fromUtf8(file.readAll()).trimmed();
	file.close();
	return value;
}

// Constant time so a local process cannot narrow the token down byte by byte.
[[nodiscard]] bool sameToken(const QString &a, const QString &b) {
	if (a.isEmpty() || a.size() != b.size()) {
		return false;
	}
	const auto left = a.toUtf8();
	const auto right = b.toUtf8();
	auto diff = 0;
	for (auto i = 0; i != left.size(); ++i) {
		diff |= (left[i] ^ right[i]);
	}
	return (diff == 0);
}

[[nodiscard]] QString fromAuthorizationHeader(const QString &header) {
	const auto prefix = u"bearer "_q;
	return header.startsWith(prefix, Qt::CaseInsensitive)
		? header.mid(prefix.size()).trimmed()
		: header.trimmed();
}

} // namespace

QString token() {
	if (GlobalToken.isEmpty()) {
		GlobalToken = load();
	}
	if (GlobalToken.isEmpty()) {
		GlobalToken = generate();
		store(GlobalToken);
	}
	return GlobalToken;
}

QString resetToken() {
	GlobalToken = generate();
	store(GlobalToken);
	return GlobalToken;
}

QString maskedToken() {
	const auto value = token();
	return (value.size() > 12)
		? (value.left(8) + u"\u2022\u2022\u2022\u2022"_q + value.right(4))
		: value;
}

bool authorized(const QString &header, const QString &fromQuery) {
	const auto expected = token();
	const auto provided = header.isEmpty()
		? fromQuery.trimmed()
		: fromAuthorizationHeader(header);
	return sameToken(expected, provided);
}

bool originAllowed(const QString &origin, quint16 port) {
	if (origin.isEmpty()) {
		// Scripts and curl send no Origin at all.
		return true;
	}
	// The built-in docs page runs on this very origin, so its requests are
	// allowed through; anything else in a browser is not.
	return (origin == u"http://127.0.0.1:%1"_q.arg(port))
		|| (origin == u"http://localhost:%1"_q.arg(port));
}

bool hostAllowed(const QString &host, quint16 port) {
	if (host.isEmpty()) {
		return false;
	}
	const auto suffix = u":"_q + QString::number(port);
	const auto name = host.endsWith(suffix)
		? host.left(host.size() - suffix.size())
		: host;
	return (name == u"127.0.0.1"_q)
		|| (name == u"localhost"_q)
		|| (name == u"[::1]"_q)
		|| (name == u"::1"_q);
}

} // namespace AyuApi
