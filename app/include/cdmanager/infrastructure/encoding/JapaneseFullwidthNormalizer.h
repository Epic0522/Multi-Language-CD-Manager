#pragma once

#include <QString>

namespace cdmanager::infrastructure::encoding {

struct FullwidthNormalizationResult {
    QString normalizedText;
    bool changed {false};
};

// 将常见的半角 ASCII / 半角片假名统一转换为日文 CD-TEXT 更稳妥的全角形式。
// 这里不碰已经从旧盘导入的原始字节，只服务于“本次重新输入/重新编码”的文本。
class JapaneseFullwidthNormalizer {
public:
    FullwidthNormalizationResult normalize(const QString& text) const;
};

}  // namespace cdmanager::infrastructure::encoding
