#pragma once

#include "cdmanager/application/CdTextTypes.h"

namespace cdmanager::application {

enum class CdTextWriteAction {
    WriteEncodedBytes,
    ReuseImportedBytes,
    SkipMissing,
    SkipEmpty
};

struct CdTextWritePlanEntry {
    PreparedCdTextField preparedField;
    CdTextWriteAction action {CdTextWriteAction::WriteEncodedBytes};
    QString reason;
};

struct CdTextWritePlan {
    QVector<CdTextWritePlanEntry> entries;

    int writableFieldCount() const;
    int skippedFieldCount() const;
};

// 负责把“准备结果”转换成后续真正写盘要吃的动作计划。
// 这一步只决定写入策略，不碰任何光驱或刻录库。
class CdTextWritePlanBuilder {
public:
    CdTextWritePlan build(const CdTextPreparationResult& preparation) const;
};

}  // namespace cdmanager::application
