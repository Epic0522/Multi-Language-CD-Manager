#include "cdmanager/infrastructure/disc/SystemDiscGateway.h"

#include <QRegularExpression>

#include "cdmanager/infrastructure/disc/DrutilCommandRunner.h"
#include "cdmanager/infrastructure/disc/DrutilOutputParser.h"

namespace cdmanager::infrastructure::disc {

namespace {

QStringList selectorForDeviceId(const QString& deviceId) {
    const QString prefix = QStringLiteral("drutil-index://");
    if (deviceId.startsWith(prefix)) {
        const QString index = deviceId.mid(prefix.size());
        if (!index.isEmpty()) {
            return {QStringLiteral("-drive"), index};
        }
    }
    return {QStringLiteral("-drive"), QStringLiteral("external")};
}

QVector<QStringList> selectorFallbacksForDeviceId(const QString& deviceId) {
    QVector<QStringList> selectors;
    selectors.append(selectorForDeviceId(deviceId));
    selectors.append({QStringLiteral("-drive"), QStringLiteral("external")});
    selectors.append(QStringList {});
    return selectors;
}

DrutilCommandResult runWithSelectorFallback(
    const DrutilCommandRunner& runner,
    const QString& deviceId,
    const QStringList& commandArguments,
    int attempts = 1,
    int delayMs = 0
) {
    DrutilCommandResult bestResult;
    for (const auto& selector : selectorFallbacksForDeviceId(deviceId)) {
        const auto result =
            attempts > 1
                ? runner.runWithRetries(selector + commandArguments, attempts, delayMs)
                : runner.run(selector + commandArguments);
        if (result.ok && !result.stdOut.trimmed().isEmpty()) {
            return result;
        }

        // 留下最后一个结果，至少能把 stderr 带回界面。
        bestResult = result;
    }
    return bestResult;
}

}  // namespace

GatewayMode SystemDiscGateway::mode() const {
    return GatewayMode::System;
}

QVector<cdmanager::domain::disc::DriveInfo> SystemDiscGateway::listDrives() const {
    return listDrivesFromDrutil();
}

cdmanager::domain::disc::DiscSnapshot SystemDiscGateway::readDisc(const QString& deviceId) const {
    // 这里先打通系统命令 -> 原始输出 -> 中间快照 的链路。
    // 真实 CD-TEXT 精准解析放到拿到你的实盘输出之后再收紧。
    cdmanager::domain::disc::DiscSnapshot snapshot;
    snapshot.sourceName = deviceId;

    const DrutilCommandRunner runner;
    const DrutilOutputParser parser;

    // Read TOC first to get track numbers and durations.
    const auto tocResult = runWithSelectorFallback(runner, deviceId, {QStringLiteral("toc")}, 2, 200);
    snapshot.rawTocOutput = tocResult.stdOut.trimmed();
    snapshot.rawTocError = tocResult.stdErr.trimmed();
    snapshot.tracks = parser.parseTracksFromToc(snapshot.rawTocOutput);

    // Then read CD-TEXT and overlay titles/artists onto the TOC tracks.
    const auto cdTextResult = runWithSelectorFallback(
        runner,
        deviceId,
        {QStringLiteral("cdtext")},
        4,
        350
    );
    snapshot.rawCdTextOutput = cdTextResult.stdOut.trimmed();
    snapshot.rawCdTextError = cdTextResult.stdErr.trimmed();
    snapshot.containsCdText = !snapshot.rawCdTextOutput.isEmpty();
    snapshot.containsJapaneseCdText = snapshot.rawCdTextOutput.contains(QRegularExpression(QStringLiteral(u"[ぁ-んァ-ヶ一-龯]")));
    parser.applyCdTextHeuristics(snapshot.rawCdTextOutput, snapshot);

    const auto statusResult = runWithSelectorFallback(runner, deviceId, {QStringLiteral("status")}, 2, 200);
    snapshot.rawStatusOutput = statusResult.stdOut.trimmed();
    snapshot.rawStatusError = statusResult.stdErr.trimmed();
    snapshot.hasMediaPresent = parser.outputSuggestsMediaPresent(snapshot.rawStatusOutput);
    snapshot.looksLikeAudioCd = parser.outputSuggestsAudioCd(snapshot.rawStatusOutput);
    snapshot.looksLikeBlankWritableDisc = parser.outputSuggestsWritableBlankMedia(snapshot.rawStatusOutput);

    if (!snapshot.tracks.isEmpty()) {
        snapshot.looksLikeAudioCd = true;
        snapshot.looksLikeBlankWritableDisc = false;
    }
    return snapshot;
}

cdmanager::domain::disc::DiscSnapshot SystemDiscGateway::readSampleDisc() const {
    cdmanager::domain::disc::DiscSnapshot snapshot;
    snapshot.sourceName = QStringLiteral("System drive placeholder");
    return snapshot;
}

QVector<cdmanager::domain::disc::DriveInfo> SystemDiscGateway::listDrivesFromDrutil() const {
    const DrutilCommandRunner runner;
    const DrutilOutputParser parser;

    const auto listResult = runner.run({QStringLiteral("list")});
    auto drives = parser.parseDriveList(listResult.stdOut);

    if (!drives.isEmpty()) {
        for (auto& drive : drives) {
            const auto statusResult = runWithSelectorFallback(runner, drive.deviceId, {QStringLiteral("status")});
            const bool hasMedia = parser.outputSuggestsMediaPresent(statusResult.stdOut);
            drive.hasMediaLoaded = hasMedia;
        }
    }

    return drives;
}

}  // namespace cdmanager::infrastructure::disc
