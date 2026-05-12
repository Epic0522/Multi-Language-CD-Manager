#include "cdmanager/application/import/DiscImportService.h"

namespace cdmanager::application::import {

namespace {

QString summaryFromSnapshot(const cdmanager::domain::disc::DiscSnapshot& snapshot) {
    return QStringLiteral("media=%1, audio-cd=%2, blank-writable=%3, tracks=%4, cdtext=%5, japanese-cdtext=%6")
        .arg(snapshot.hasMediaPresent ? QStringLiteral("yes") : QStringLiteral("no"))
        .arg(snapshot.looksLikeAudioCd ? QStringLiteral("yes") : QStringLiteral("no"))
        .arg(snapshot.looksLikeBlankWritableDisc ? QStringLiteral("yes") : QStringLiteral("no"))
        .arg(snapshot.tracks.size())
        .arg(snapshot.containsCdText ? QStringLiteral("yes") : QStringLiteral("no"))
        .arg(snapshot.containsJapaneseCdText ? QStringLiteral("yes") : QStringLiteral("no"));
}

}  // namespace

DiscImportService::DiscImportService(
    const cdmanager::infrastructure::disc::DiscDeviceGateway& gateway
)
    : m_gateway(gateway) {
}

QVector<cdmanager::domain::disc::DriveInfo> DiscImportService::availableDrives() const {
    return m_gateway.listDrives();
}

cdmanager::domain::project::CdProject DiscImportService::importFromDrive(const QString& deviceId) const {
    return mapSnapshot(m_gateway.readDisc(deviceId));
}

cdmanager::domain::project::CdProject DiscImportService::importSampleProject() const {
    return mapSnapshot(m_gateway.readSampleDisc());
}

DiscImportResult DiscImportService::initialImport() const {
    const auto drives = availableDrives();
    if (drives.isEmpty()) {
        return {
            DiscImportStatus::FallbackSample,
            QStringLiteral("未检测到真实光驱，当前使用样本盘数据驱动界面。"),
            QStringLiteral("media=no, audio-cd=no, tracks=3, cdtext=yes, japanese-cdtext=yes"),
            importSampleProject(),
            QStringLiteral("No system drive detected at startup."),
        };
    }

    const auto snapshot = m_gateway.readDisc(drives.first().deviceId);
    const auto project = mapSnapshot(snapshot);
    QString diagnostics;
    if (!snapshot.rawStatusOutput.isEmpty()) {
        diagnostics += QStringLiteral("[drutil status]\n%1\n\n").arg(snapshot.rawStatusOutput);
    }
    if (!snapshot.rawStatusError.isEmpty()) {
        diagnostics += QStringLiteral("[drutil status stderr]\n%1\n\n").arg(snapshot.rawStatusError);
    }
    if (!snapshot.rawTocOutput.isEmpty()) {
        diagnostics += QStringLiteral("[drutil toc]\n%1\n\n").arg(snapshot.rawTocOutput);
    }
    if (!snapshot.rawTocError.isEmpty()) {
        diagnostics += QStringLiteral("[drutil toc stderr]\n%1\n\n").arg(snapshot.rawTocError);
    }
    if (!snapshot.rawCdTextOutput.isEmpty()) {
        diagnostics += QStringLiteral("[drutil cdtext]\n%1\n").arg(snapshot.rawCdTextOutput);
    }
    if (!snapshot.rawCdTextError.isEmpty()) {
        diagnostics += QStringLiteral("[drutil cdtext stderr]\n%1\n").arg(snapshot.rawCdTextError);
    }

    if (!snapshot.hasMediaPresent) {
        return {
            DiscImportStatus::NoMediaLoaded,
            QStringLiteral("已检测到光驱，但当前没有插入光盘。"),
            summaryFromSnapshot(snapshot),
            cdmanager::domain::project::CdProject{},
            diagnostics.trimmed(),
        };
    }

    if (snapshot.looksLikeBlankWritableDisc) {
        return {
            DiscImportStatus::BlankWritableMedia,
            QStringLiteral("已插入空白可写光盘，可以直接前往刻录页开始创建音频 CD。"),
            summaryFromSnapshot(snapshot),
            cdmanager::domain::project::CdProject{},
            diagnostics.trimmed(),
        };
    }

    if (project.tracks.isEmpty() && project.albumTitle.isEmpty() && project.albumArtist.isEmpty()) {
        return {
            DiscImportStatus::DriveVisibleButReadNotImplemented,
            QStringLiteral("已经检测到系统光驱，但真实读盘和 CD-TEXT 提取尚未接入。"),
            summaryFromSnapshot(snapshot),
            importSampleProject(),
            diagnostics.trimmed(),
        };
    }

    return {
        DiscImportStatus::Success,
        QStringLiteral("已从系统光驱导入项目数据。"),
        summaryFromSnapshot(snapshot),
        project,
        diagnostics.trimmed(),
    };
}

cdmanager::domain::project::CdProject DiscImportService::mapSnapshot(
    const cdmanager::domain::disc::DiscSnapshot& snapshot
) const {
    cdmanager::domain::project::CdProject project;
    project.albumTitle = snapshot.albumTitle;
    project.albumArtist = snapshot.albumArtist;
    project.cdTextLanguage = snapshot.containsJapaneseCdText
        ? cdmanager::domain::cdtext::CdTextLanguage::Japanese
        : cdmanager::domain::cdtext::CdTextLanguage::Latin;
    project.tracks = snapshot.tracks;
    return project;
}

}  // namespace cdmanager::application::import
