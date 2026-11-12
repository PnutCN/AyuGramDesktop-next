// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

class HistoryItem;

namespace AyuMedia {

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

struct SavedMedia {
	std::string path;
	std::string mimeType;
	int documentType = MediaNone;
};

// WHY: copies only what is already cached and never starts a download,
// so media that was never opened keeps its type but gets no file.
[[nodiscard]] SavedMedia trySaveLocal(not_null<HistoryItem*> item);

void removeLocal(not_null<HistoryItem*> item);

} // namespace AyuMedia
