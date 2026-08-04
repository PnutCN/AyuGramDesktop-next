// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/data/media_storage.h"

#include "ayu/ayu_settings.h"
#include "ayu/utils/telegram_helpers.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "history/history.h"
#include "history/history_item.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeDatabase>

namespace AyuMedia {
namespace {

[[nodiscard]] int documentTypeOf(not_null<DocumentData*> document) {
	if (document->sticker()) {
		return MediaSticker;
	} else if (document->isVoiceMessage()) {
		return MediaVoice;
	} else if (document->isVideoFile() || document->isVideoMessage()) {
		return MediaVideo;
	} else if (document->isAnimation()) {
		return MediaGif;
	} else if (document->isAudioFile()) {
		return MediaAudio;
	}
	return MediaFile;
}

[[nodiscard]] QString extensionFor(const QString &mimeType, const QString &original) {
	const auto fromName = QFileInfo(original).suffix();
	if (!fromName.isEmpty()) {
		return fromName;
	}
	const auto known = QMimeDatabase().mimeTypeForName(mimeType);
	const auto suffix = known.preferredSuffix();
	return suffix.isEmpty() ? u"bin"_q : suffix;
}

[[nodiscard]] QString targetPath(
		not_null<HistoryItem*> item,
		const QString &extension) {
	const auto dialogId = getDialogIdFromPeer(item->history()->peer);
	const auto name = u"%1_%2.%3"_q
		.arg(dialogId)
		.arg(item->id.bare)
		.arg(extension);
	return mediaDirectory() + name;
}

[[nodiscard]] bool writeBytes(const QString &path, const QByteArray &bytes) {
	if (bytes.isEmpty()) {
		return false;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	const auto written = file.write(bytes);
	file.close();
	return written == bytes.size();
}

// Copying is preferred over reading the whole file into memory, which would
// spike RAM on large videos deleted in bulk.
[[nodiscard]] bool copyFile(const QString &from, const QString &to) {
	if (from.isEmpty() || !QFileInfo::exists(from)) {
		return false;
	}
	QFile::remove(to);
	return QFile::copy(from, to);
}

[[nodiscard]] std::string relativePath(const QString &absolute) {
	const auto working = cWorkingDir();
	return absolute.startsWith(working)
		? absolute.mid(working.size()).toStdString()
		: absolute.toStdString();
}

[[nodiscard]] SavedMedia savePhoto(
		not_null<HistoryItem*> item,
		not_null<PhotoData*> photo) {
	auto result = SavedMedia();
	result.documentType = MediaPhoto;
	result.mimeType = "image/jpeg";

	const auto view = photo->createMediaView();
	auto bytes = view->imageBytes(Data::PhotoSize::Large);
	if (bytes.isEmpty()) {
		bytes = view->imageBytes(Data::PhotoSize::Thumbnail);
	}
	if (bytes.isEmpty()) {
		return result;
	}
	const auto path = targetPath(item, u"jpg"_q);
	if (writeBytes(path, bytes)) {
		result.path = relativePath(path);
	}
	return result;
}

[[nodiscard]] SavedMedia saveDocument(
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document) {
	auto result = SavedMedia();
	result.documentType = documentTypeOf(document);
	result.mimeType = document->mimeString().toStdString();

	const auto extension = extensionFor(
		document->mimeString(),
		document->filename());
	const auto path = targetPath(item, extension);

	if (copyFile(document->filepath(true), path)) {
		result.path = relativePath(path);
		return result;
	}
	const auto view = document->createMediaView();
	if (writeBytes(path, view->bytes())) {
		result.path = relativePath(path);
	}
	return result;
}

} // namespace

QString mediaDirectory() {
	const auto path = cWorkingDir() + u"tdata/ayu_media/"_q;
	QDir().mkpath(path);
	return path;
}

SavedMedia trySaveLocal(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media || !AyuSettings::getInstance().saveDeletedMedia()) {
		return SavedMedia();
	}
	if (const auto document = media->document()) {
		return saveDocument(item, document);
	} else if (const auto photo = media->photo()) {
		return savePhoto(item, photo);
	}
	return SavedMedia();
}

} // namespace AyuMedia
