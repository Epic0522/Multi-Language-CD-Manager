#include "cdmanager/application/CdTextWritePlanBuilder.h"

namespace cdmanager::application {

int CdTextWritePlan::writableFieldCount() const {
    int count = 0;
    for (const auto& entry : entries) {
        if (entry.action == CdTextWriteAction::WriteEncodedBytes ||
            entry.action == CdTextWriteAction::ReuseImportedBytes) {
            ++count;
        }
    }
    return count;
}

int CdTextWritePlan::skippedFieldCount() const {
    int count = 0;
    for (const auto& entry : entries) {
        if (entry.action == CdTextWriteAction::SkipMissing ||
            entry.action == CdTextWriteAction::SkipEmpty) {
            ++count;
        }
    }
    return count;
}

CdTextWritePlan CdTextWritePlanBuilder::build(const CdTextPreparationResult& preparation) const {
    CdTextWritePlan plan;

    for (const auto& preparedField : preparation.preparedFields) {
        CdTextWritePlanEntry entry;
        entry.preparedField = preparedField;

        switch (preparedField.field.valueState) {
            case cdmanager::domain::cdtext::CdTextValueState::Present:
                if (preparedField.reusedPreservedBytes) {
                    entry.action = CdTextWriteAction::ReuseImportedBytes;
                    entry.reason = QStringLiteral("优先沿用导入盘里的原始字节表示。");
                } else {
                    entry.action = CdTextWriteAction::WriteEncodedBytes;
                    entry.reason = QStringLiteral("使用当前编辑后的文本重新编码写入。");
                }
                break;
            case cdmanager::domain::cdtext::CdTextValueState::MissingOnDisc:
                entry.action = CdTextWriteAction::SkipMissing;
                entry.reason = QStringLiteral("原盘没有这个字段，默认不强行补写。");
                break;
            case cdmanager::domain::cdtext::CdTextValueState::EmptyByEdit:
                entry.action = CdTextWriteAction::SkipEmpty;
                entry.reason = QStringLiteral("用户主动留空，本次写盘跳过该字段。");
                break;
        }

        plan.entries.append(entry);
    }

    return plan;
}

}  // namespace cdmanager::application
