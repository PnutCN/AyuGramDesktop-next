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

[[nodiscard]] QString extensionFor(
		const QString &mimeType,
		const QString &original) {
	const auto fromName = QFileInfo(original).suffix();
	if (!fromName.isEmpty()) {
		return fromName;
	}
	const auto suffix = QMimeDatabase().mimeTypeForName(
		mimeType).preferredSuffix();
	return suffix.isEmpty() ? u"bin"_q : suffix;
}

[[nodiscard]] QString mediaDirectory() {
	const auto path = cWorkingDir() + u"tdata/ayu_media/"_q;
	QDir().mkpath(path);
	return path;
}

[[nodiscard]] QString fileStem(not_null<HistoryItem*> item) {
	const auto dialogId = getDialogIdFromPeer(item->history()->peer);
	return u"%1_%2"_q.arg(dialogId).arg(item->id.bare);
}

[[nodiscard]] QString targetPath(
		not_null<HistoryItem*> item,
		const QString &extension) {
	return mediaDirectory() + fileStem(item) + '.' + extension;
}

[[nodiscard]] bool writeBytes(const QString &path, const QByteArray &bytes) {
	if (bytes.isEmpty()) {
		return false;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}
	return (file.write(bytes) == bytes.size());
}

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
	auto result = SavedMedia{
		.mimeType = "image/jpeg",
		.documentType = MediaPhoto,
	};
	const auto view = photo->createMediaView();
	if (!view->loaded()) {
		return result; // a partial progressive download is not a usable file
	}
	const auto path = targetPath(item, u"jpg"_q);
	if (writeBytes(path, view->imageBytes(Data::PhotoSize::Large))) {
		result.path = relativePath(path);
	}
	return result;
}

[[nodiscard]] SavedMedia saveDocument(
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document) {
	auto result = SavedMedia{
		.mimeType = document->mimeString().toStdString(),
		.documentType = documentTypeOf(document),
	};
	const auto path = targetPath(
		item,
		extensionFor(document->mimeString(), document->filename()));
	if (copyFile(document->filepath(true), path)
		|| writeBytes(path, document->createMediaView()->bytes())) {
		result.path = relativePath(path);
	}
	return result;
}

} // namespace

SavedMedia trySaveLocal(not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media || !AyuSettings::getInstance().saveDeletedMedia()) {
		return SavedMedia();
	} else if (const auto document = media->document()) {
		return saveDocument(item, document);
	} else if (const auto photo = media->photo()) {
		return savePhoto(item, photo);
	}
	return SavedMedia();
}

void removeLocal(not_null<HistoryItem*> item) {
	const auto entries = QDir(mediaDirectory()).entryInfoList(
		QStringList{ fileStem(item) + u".*"_q },
		QDir::Files);
	for (const auto &entry : entries) {
		if (!QFile::remove(entry.absoluteFilePath())) {
			LOG(("AyuMedia: failed to remove %1").arg(
				entry.absoluteFilePath()));
		}
	}
}

} // namespace AyuMedia
