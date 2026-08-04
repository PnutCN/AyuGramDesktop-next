/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/const_string.h"

#define TDESKTOP_REQUESTED_ALPHA_VERSION (0ULL)

#ifdef TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION TDESKTOP_REQUESTED_ALPHA_VERSION
#else // TDESKTOP_ALLOW_CLOSED_ALPHA
#define TDESKTOP_ALPHA_VERSION (0ULL)
#endif // TDESKTOP_ALLOW_CLOSED_ALPHA

// used in Updater.cpp and Setup.iss for Windows
// AppName also decides the data directory, so it must differ from the
// upstream AyuGram build to let both run side by side without sharing
// tdata and ayudata.db.
constexpr auto AppId = "{7B3A5C21-9E44-4F1D-B8C6-2D5E9A1F4C80}"_cs;
constexpr auto AppNameOld = "AyuGram Next for Windows"_cs;
constexpr auto AppName = "AyuGram Next"_cs;
constexpr auto AppFile = "AyuGramNext"_cs;
constexpr auto AppVersion = 7000009;
constexpr auto AppVersionStr = "7.0.9";
constexpr auto AppBetaVersion = false;
constexpr auto AppAlphaVersion = TDESKTOP_ALPHA_VERSION;
