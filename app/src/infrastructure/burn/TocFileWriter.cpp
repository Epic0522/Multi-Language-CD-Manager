#include "cdmanager/infrastructure/burn/TocFileWriter.h"

#include <QFile>
#include <QStringList>

#include "cdmanager/infrastructure/encoding/MsJisCodec.h"
#include "cdmanager/infrastructure/encoding/JapaneseFullwidthNormalizer.h"

namespace cdmanager::infrastructure::burn {

namespace {

static QString escapeTocString(const QString& s) {
    QString escaped = s;
    escaped.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    escaped.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return escaped;
}

static QByteArray encodeTocText(const QString& text,
                                const QString& language,
                                TocWriterTarget target) {
    Q_UNUSED(language);
    // 这里写的是“中间 TOC 文本文件”，不是最终刻到盘上的 CD-TEXT 字节。
    // 无论是 drutil 还是 cdrdao，TOC 源文件本身都更适合保持 UTF-8，
    // 真正的日文编码语义交给 LANGUAGE_MAP / ENCODING_MS_JIS / 刻录后端。
    if (target == TocWriterTarget::Cdrdao) {
        return text.toUtf8();
    }
    return text.toUtf8();
}

const cdmanager::application::CdTextWritePayloadField* findPayloadField(
    const QVector<cdmanager::application::CdTextWritePayloadField>& fields,
    cdmanager::domain::cdtext::CdTextFieldId id)
{
    for (const auto& field : fields) {
        if (field.preparedField.field.id == id) {
            return &field;
        }
    }
    return nullptr;
}

bool payloadHasFieldId(const QVector<cdmanager::application::CdTextWritePayloadField>& fields,
                       cdmanager::domain::cdtext::CdTextFieldId id)
{
    return findPayloadField(fields, id) != nullptr;
}

const cdmanager::application::CdTextWritePayloadTrack* findTrackPayload(
    const cdmanager::application::CdTextWritePayload& payload,
    int trackNumber)
{
    for (const auto& track : payload.tracks) {
        if (track.trackNumber == trackNumber) {
            return &track;
        }
    }
    return nullptr;
}

QString cdrdaoFieldValueLiteral(const cdmanager::application::PreparedCdTextField& preparedField) {
    return QStringLiteral("\"%1\"").arg(escapeTocString(preparedField.effectiveValue));
}

QString cdrdaoEmptyFieldLiteral(cdmanager::domain::cdtext::CdTextLanguage language,
                                cdmanager::domain::cdtext::CdTextFieldId fieldId) {
    Q_UNUSED(language);
    Q_UNUSED(fieldId);
    return QStringLiteral("\"\"");
}

static QString normalizedTocValue(const QString& text, bool useJapaneseFullwidth) {
    if (!useJapaneseFullwidth) {
        return text;
    }

    const cdmanager::infrastructure::encoding::JapaneseFullwidthNormalizer fullwidthNormalizer;
    return fullwidthNormalizer.normalize(text).normalizedText;
}

static QString cdrdaoLanguageCode(const QString& language) {
    return language.compare(QStringLiteral("JP"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("105")
        : QStringLiteral("EN");
}

static QString cdrdaoEncodingDirective(const QString& language) {
    return language.compare(QStringLiteral("JP"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("    ENCODING_MS_JIS\n")
        : QStringLiteral("    ENCODING_ISO_8859_1\n");
}

QString cdrdaoLanguageMap(const QString& language, TocWriterTarget target) {
    const QString languageToken = target == TocWriterTarget::Cdrdao
        ? cdrdaoLanguageCode(language)
        : language;
    return QStringLiteral("  LANGUAGE_MAP { 0: %1 }\n").arg(languageToken);
}

}  // namespace

bool TocFileWriter::write(const QString& filePath,
                          const cdmanager::domain::project::CdProject& project,
                          const cdmanager::application::CdTextWritePayload& writePayload,
                          const QStringList& wavFilePaths,
                          const QString& language,
                          TocWriterTarget target) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    const QString tocText = buildTocText(project, writePayload, wavFilePaths, language, target);
    const QByteArray encoded = encodeTocText(tocText, language, target);
    if (encoded.isEmpty() && !tocText.isEmpty()) {
        return false;
    }

    return file.write(encoded) == encoded.size();
}

QString TocFileWriter::buildTocText(const cdmanager::domain::project::CdProject& project,
                                    const cdmanager::application::CdTextWritePayload& writePayload,
                                    const QStringList& wavFilePaths,
                                    const QString& language,
                                    TocWriterTarget target) {
    const bool useJapaneseFullwidth = language.compare(QStringLiteral("JP"), Qt::CaseInsensitive) == 0;
    const auto normalizeForLanguage = [&](const QString& text) {
        return normalizedTocValue(text, useJapaneseFullwidth);
    };

    QString tocText;
    tocText += QStringLiteral("CD_DA\n\n");

    const bool cdrdaoMode = target == TocWriterTarget::Cdrdao;
    const bool mustDefineAlbumTitleForAll = cdrdaoMode && (
        payloadHasFieldId(writePayload.albumWritableFields, cdmanager::domain::cdtext::CdTextFieldId::AlbumTitle)
        || payloadHasFieldId(writePayload.albumSkippedFields, cdmanager::domain::cdtext::CdTextFieldId::AlbumTitle)
    );
    const bool mustDefineAlbumArtistForAll = cdrdaoMode && (
        payloadHasFieldId(writePayload.albumWritableFields, cdmanager::domain::cdtext::CdTextFieldId::AlbumArtist)
        || payloadHasFieldId(writePayload.albumSkippedFields, cdmanager::domain::cdtext::CdTextFieldId::AlbumArtist)
    );
    bool mustDefineTrackTitleForAll = false;
    bool mustDefineTrackArtistForAll = false;
    if (cdrdaoMode) {
        for (const auto& trackPayload : writePayload.tracks) {
            if (payloadHasFieldId(trackPayload.writableFields, cdmanager::domain::cdtext::CdTextFieldId::TrackTitle)
                || payloadHasFieldId(trackPayload.skippedFields, cdmanager::domain::cdtext::CdTextFieldId::TrackTitle)) {
                mustDefineTrackTitleForAll = true;
            }
            if (payloadHasFieldId(trackPayload.writableFields, cdmanager::domain::cdtext::CdTextFieldId::TrackArtist)
                || payloadHasFieldId(trackPayload.skippedFields, cdmanager::domain::cdtext::CdTextFieldId::TrackArtist)) {
                mustDefineTrackArtistForAll = true;
            }
        }
    }

    bool hasAnyCdText = !project.albumTitle.isEmpty() || !project.albumArtist.isEmpty();
    if (!hasAnyCdText) {
        for (const auto& track : project.tracks) {
            if (!track.title.isEmpty() || !track.artist.isEmpty()) {
                hasAnyCdText = true;
                break;
            }
        }
    }

    // 只要整张盘里存在任意 CD-TEXT，就先写一个总 LANGUAGE_MAP。
    // 不然有些工具会把只有轨道级 CD_TEXT 的 .toc 当成无效镜像。
    if (hasAnyCdText) {
        tocText += QStringLiteral("CD_TEXT {\n");
        tocText += cdrdaoLanguageMap(language, target);
        if (target == TocWriterTarget::Cdrdao) {
            tocText += QStringLiteral("  LANGUAGE 0 {\n");
            tocText += cdrdaoEncodingDirective(language);
            if (const auto* titleField = findPayloadField(
                    writePayload.albumWritableFields,
                    cdmanager::domain::cdtext::CdTextFieldId::AlbumTitle)) {
                tocText += QStringLiteral("    TITLE %1\n")
                    .arg(cdrdaoFieldValueLiteral(titleField->preparedField));
            } else if (mustDefineAlbumTitleForAll || mustDefineTrackTitleForAll) {
                tocText += QStringLiteral("    TITLE %1\n")
                    .arg(cdrdaoEmptyFieldLiteral(cdmanager::domain::cdtext::CdTextLanguage::Japanese,
                                                 cdmanager::domain::cdtext::CdTextFieldId::AlbumTitle));
            }
            if (const auto* artistField = findPayloadField(
                    writePayload.albumWritableFields,
                    cdmanager::domain::cdtext::CdTextFieldId::AlbumArtist)) {
                tocText += QStringLiteral("    PERFORMER %1\n")
                    .arg(cdrdaoFieldValueLiteral(artistField->preparedField));
            } else if (mustDefineAlbumArtistForAll || mustDefineTrackArtistForAll) {
                tocText += QStringLiteral("    PERFORMER %1\n")
                    .arg(cdrdaoEmptyFieldLiteral(cdmanager::domain::cdtext::CdTextLanguage::Japanese,
                                                 cdmanager::domain::cdtext::CdTextFieldId::AlbumArtist));
            }
            tocText += QStringLiteral("  }\n");
        } else {
            tocText += QStringLiteral("  LANGUAGE 0 {\n");
            if (!project.albumTitle.isEmpty())
                tocText += QStringLiteral("    TITLE \"%1\"\n")
                    .arg(escapeTocString(normalizeForLanguage(project.albumTitle)));
            if (!project.albumArtist.isEmpty())
                tocText += QStringLiteral("    PERFORMER \"%1\"\n")
                    .arg(escapeTocString(normalizeForLanguage(project.albumArtist)));
            tocText += QStringLiteral("  }\n");
        }
        tocText += QStringLiteral("}\n\n");
    }

    // Tracks.
    for (int i = 0; i < project.tracks.size() && i < wavFilePaths.size(); ++i) {
        const auto& track = project.tracks[i];

        tocText += QStringLiteral("TRACK AUDIO\n");
        if (target == TocWriterTarget::Cdrdao) {
            tocText += QStringLiteral("NO COPY\n");
            tocText += QStringLiteral("NO PRE_EMPHASIS\n");
            tocText += QStringLiteral("TWO_CHANNEL_AUDIO\n");
        }

        const bool shouldEmitTrackTextBlock = cdrdaoMode
            ? (mustDefineTrackTitleForAll || mustDefineTrackArtistForAll)
            : (!track.title.isEmpty() || !track.artist.isEmpty());
        if (shouldEmitTrackTextBlock) {
            tocText += QStringLiteral("CD_TEXT {\n");
            if (target == TocWriterTarget::Cdrdao) {
                tocText += QStringLiteral("  LANGUAGE 0 {\n");
                tocText += cdrdaoEncodingDirective(language);
                if (const auto* trackPayload = findTrackPayload(writePayload, track.number)) {
                    if (const auto* titleField = findPayloadField(
                            trackPayload->writableFields,
                            cdmanager::domain::cdtext::CdTextFieldId::TrackTitle)) {
                        tocText += QStringLiteral("    TITLE %1\n")
                            .arg(cdrdaoFieldValueLiteral(titleField->preparedField));
                    } else if (mustDefineTrackTitleForAll) {
                        tocText += QStringLiteral("    TITLE %1\n")
                            .arg(cdrdaoEmptyFieldLiteral(cdmanager::domain::cdtext::CdTextLanguage::Japanese,
                                                         cdmanager::domain::cdtext::CdTextFieldId::TrackTitle));
                    }
                    if (const auto* artistField = findPayloadField(
                            trackPayload->writableFields,
                            cdmanager::domain::cdtext::CdTextFieldId::TrackArtist)) {
                        tocText += QStringLiteral("    PERFORMER %1\n")
                            .arg(cdrdaoFieldValueLiteral(artistField->preparedField));
                    } else if (mustDefineTrackArtistForAll) {
                        tocText += QStringLiteral("    PERFORMER %1\n")
                            .arg(cdrdaoEmptyFieldLiteral(cdmanager::domain::cdtext::CdTextLanguage::Japanese,
                                                         cdmanager::domain::cdtext::CdTextFieldId::TrackArtist));
                    }
                } else {
                    if (mustDefineTrackTitleForAll) {
                        tocText += QStringLiteral("    TITLE %1\n")
                            .arg(cdrdaoEmptyFieldLiteral(cdmanager::domain::cdtext::CdTextLanguage::Japanese,
                                                         cdmanager::domain::cdtext::CdTextFieldId::TrackTitle));
                    }
                    if (mustDefineTrackArtistForAll) {
                        tocText += QStringLiteral("    PERFORMER %1\n")
                            .arg(cdrdaoEmptyFieldLiteral(cdmanager::domain::cdtext::CdTextLanguage::Japanese,
                                                         cdmanager::domain::cdtext::CdTextFieldId::TrackArtist));
                    }
                }
                tocText += QStringLiteral("  }\n");
            } else {
                tocText += QStringLiteral("  LANGUAGE 0 {\n");
                if (!track.title.isEmpty())
                    tocText += QStringLiteral("    TITLE \"%1\"\n")
                        .arg(escapeTocString(normalizeForLanguage(track.title)));
                if (!track.artist.isEmpty())
                    tocText += QStringLiteral("    PERFORMER \"%1\"\n")
                        .arg(escapeTocString(normalizeForLanguage(track.artist)));
                tocText += QStringLiteral("  }\n");
            }
            tocText += QStringLiteral("}\n");
        }

        if (target == TocWriterTarget::Cdrdao && project.trackGapSeconds > 0 && i > 0) {
            tocText += QStringLiteral("PREGAP 0:%1:0\n").arg(project.trackGapSeconds);
        }
        tocText += QStringLiteral("FILE \"%1\" 0\n\n").arg(wavFilePaths[i]);
    }
    return tocText;
}

}  // namespace cdmanager::infrastructure::burn
