#pragma once

#include <QVector>

#include "cdmanager/application/CdTextWritePlanBuilder.h"

namespace cdmanager::application {

struct CdTextWritePayloadField {
    PreparedCdTextField preparedField;
    CdTextWriteAction action {CdTextWriteAction::WriteEncodedBytes};
    QString reason;
};

struct CdTextWritePayloadTrack {
    int trackNumber {0};
    QVector<CdTextWritePayloadField> writableFields;
    QVector<CdTextWritePayloadField> skippedFields;
};

struct CdTextWritePayload {
    QVector<CdTextWritePayloadField> albumWritableFields;
    QVector<CdTextWritePayloadField> albumSkippedFields;
    QVector<CdTextWritePayloadTrack> tracks;

    int writableFieldCount() const;
    int skippedFieldCount() const;
    int writableByteCount() const;
};

// 把线性的写入计划重新整理成“专辑 + 轨道”的结构，
// 让后续刻录后端只关心要写哪些字段，不用回头解析 UI 文案。
class CdTextWritePayloadBuilder {
public:
    CdTextWritePayload build(const CdTextWritePlan& plan) const;
};

}  // namespace cdmanager::application
