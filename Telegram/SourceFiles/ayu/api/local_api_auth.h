// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <QtCore/QString>

namespace AyuApi {

// The token lives in its own file rather than ayu_settings.json: that file
// gets pasted into bug reports, and leaking the token leaks every chat.
[[nodiscard]] QString token();
[[nodiscard]] QString resetToken();

// Shortened form for the settings row, so a screenshot does not leak it.
[[nodiscard]] QString maskedToken();

// Checks the Authorization / X-Ayu-Token header or the token query item.
[[nodiscard]] bool authorized(const QString &header, const QString &fromQuery);

// A page in a browser can reach 127.0.0.1, so requests carrying an Origin are
// refused outright, and Host is pinned to loopback to stop DNS rebinding.
[[nodiscard]] bool originAllowed(const QString &origin, quint16 port);
[[nodiscard]] bool hostAllowed(const QString &host, quint16 port);

}
