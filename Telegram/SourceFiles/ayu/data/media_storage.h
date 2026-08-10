// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/data/entities.h"

namespace AyuMedia {

// Stored in the documentType column of DeletedMessage / EditedMessage.
enum MediaType : int {
	MediaNone = 0,
	MediaPhoto = 1,
	MediaVideo = 2,
	MediaVoice = 3,
	MediaAudio = 4,
	MediaSticker = 5,
	MediaGif = 6,
	MediaFile = 7,
};

struct SavedMedia
{
	std::string path; // relative to the working dir, empty when not cached
	std::string mimeType;
	int documentType = MediaNone;
};

// Copies the media out of the local cache. Never starts a download, so a
// message whose media was never opened keeps its metadata but no file.
[[nodiscard]] SavedMedia trySaveLocal(not_null<HistoryItem*> item);

// Reports what trySaveLocal would produce without touching the disk. Listing
// endpoints must use this — probing a few hundred messages with trySaveLocal
// would copy a few hundred files.
[[nodiscard]] SavedMedia probeLocal(not_null<HistoryItem*> item);

[[nodiscard]] QString mediaDirectory();

// Turns a stored relative path back into an absolute one, or returns an empty
// string when the file is gone or the path escapes the media directory. Every
// consumer of the mediaPath column must go through this — the column is data,
// and a corrupted row must not be able to reach arbitrary files.
[[nodiscard]] QString resolveLocalPath(const QString &stored);

// Deletes whatever trySaveLocal wrote for this message. Called when a stored
// message is dropped, so the media directory does not accumulate orphans.
void removeLocal(not_null<HistoryItem*> item);

}
