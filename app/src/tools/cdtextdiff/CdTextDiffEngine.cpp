#include "cdmanager/tools/cdtextdiff/CdTextDiffEngine.h"

#include <algorithm>

namespace cdmanager::tools::cdtextdiff {

namespace {

void appendByteDelta(CdTextDiffPackDelta& delta, int byteOffset, int leftValue, int rightValue) {
    if (leftValue != rightValue) {
        delta.byteDeltas.append({byteOffset, leftValue, rightValue});
    }
}

QVector<int> collapsedPackTypeGroups(const ParsedCdTextDocument& document) {
    QVector<int> groups;
    int lastType = -1;
    for (const auto& pack : document.packs) {
        const int type = static_cast<int>(pack.packType());
        if (groups.isEmpty() || type != lastType) {
            groups.append(type);
            lastType = type;
        }
    }
    return groups;
}

int packCountForType(const ParsedCdTextDocument& document, std::uint8_t packType) {
    int count = 0;
    for (const auto& pack : document.packs) {
        if (pack.packType() == packType) {
            ++count;
        }
    }
    return count;
}

QVector<int> uniquePackSizes(const ParsedCdTextDocument& document) {
    QVector<int> sizes;
    for (const auto& pack : document.packs) {
        const int size = pack.bytes.size();
        if (!sizes.contains(size)) {
            sizes.append(size);
        }
    }
    std::sort(sizes.begin(), sizes.end());
    return sizes;
}

}

CdTextDiffReport CdTextDiffEngine::compare(const ParsedCdTextDocument& left,
                                           const ParsedCdTextDocument& right,
                                           CompareMode mode) const {
    CdTextDiffReport report;
    report.left = left;
    report.right = right;
    report.mode = mode;
    report.identical = true;

    if (left.reconstructedFromReferenceMetadata || right.reconstructedFromReferenceMetadata) {
        report.documentNotes.append(
            QStringLiteral("At least one side is rebuilt from captured reference metadata rather than parsed from raw lead-in pack bytes.")
        );
    }

    if (mode == CompareMode::Schema) {
        if (left.blockCount() != right.blockCount()) {
            report.identical = false;
            report.documentNotes.append(
                QStringLiteral("Block count differs: left=%1 right=%2")
                    .arg(left.blockCount())
                    .arg(right.blockCount())
            );
        }

        if (left.hasAnyCrc() != right.hasAnyCrc()) {
            report.identical = false;
            report.documentNotes.append(
                QStringLiteral("CRC presence differs: left=%1 right=%2")
                    .arg(left.hasAnyCrc() ? QStringLiteral("yes") : QStringLiteral("no"))
                    .arg(right.hasAnyCrc() ? QStringLiteral("yes") : QStringLiteral("no"))
            );
        }

        const auto leftGroups = collapsedPackTypeGroups(left);
        const auto rightGroups = collapsedPackTypeGroups(right);
        if (leftGroups != rightGroups) {
            report.identical = false;
            QStringList leftText;
            QStringList rightText;
            for (int value : leftGroups) {
                leftText.append(QStringLiteral("0x%1").arg(value, 2, 16, QLatin1Char('0')).toUpper());
            }
            for (int value : rightGroups) {
                rightText.append(QStringLiteral("0x%1").arg(value, 2, 16, QLatin1Char('0')).toUpper());
            }
            report.documentNotes.append(
                QStringLiteral("Collapsed pack type groups differ: left=[%1] right=[%2]")
                    .arg(leftText.join(QStringLiteral(", ")))
                    .arg(rightText.join(QStringLiteral(", ")))
            );
        }

        const int leftSizeInfoPackCount = packCountForType(left, 0x8F);
        const int rightSizeInfoPackCount = packCountForType(right, 0x8F);
        if (leftSizeInfoPackCount != rightSizeInfoPackCount) {
            report.identical = false;
            report.documentNotes.append(
                QStringLiteral("SIZE_INFO pack count differs: left=%1 right=%2")
                    .arg(leftSizeInfoPackCount)
                    .arg(rightSizeInfoPackCount)
            );
        }

        const auto leftPackSizes = uniquePackSizes(left);
        const auto rightPackSizes = uniquePackSizes(right);
        if (leftPackSizes != rightPackSizes) {
            report.identical = false;
            QStringList leftText;
            QStringList rightText;
            for (int value : leftPackSizes) {
                leftText.append(QString::number(value));
            }
            for (int value : rightPackSizes) {
                rightText.append(QString::number(value));
            }
            report.documentNotes.append(
                QStringLiteral("Pack byte sizes differ: left=[%1] right=[%2]")
                    .arg(leftText.join(QStringLiteral(", ")))
                    .arg(rightText.join(QStringLiteral(", ")))
            );
        }

        return report;
    }

    if (left.packCount() != right.packCount()) {
        report.identical = false;
        report.documentNotes.append(
            QStringLiteral("Pack count differs: left=%1 right=%2")
                .arg(left.packCount())
                .arg(right.packCount())
        );
    }

    const int maxPacks = std::max(left.packCount(), right.packCount());
    for (int packIndex = 0; packIndex < maxPacks; ++packIndex) {
        CdTextDiffPackDelta delta;
        delta.packIndex = packIndex;

        const bool hasLeft = packIndex < left.packCount();
        const bool hasRight = packIndex < right.packCount();

        if (!hasLeft || !hasRight) {
            delta.reason = !hasLeft
                ? QStringLiteral("Pack missing on left side.")
                : QStringLiteral("Pack missing on right side.");
            if (hasLeft) {
                delta.left = left.packs.at(packIndex);
            }
            if (hasRight) {
                delta.right = right.packs.at(packIndex);
            }
            report.packDeltas.append(delta);
            report.identical = false;
            continue;
        }

        delta.left = left.packs.at(packIndex);
        delta.right = right.packs.at(packIndex);

        if (mode == CompareMode::Structure) {
            appendByteDelta(delta, 0, delta.left.packType(), delta.right.packType());
            appendByteDelta(delta, 1, delta.left.trackNumber(), delta.right.trackNumber());
            appendByteDelta(delta, 2, delta.left.sequenceNumber(), delta.right.sequenceNumber());
            appendByteDelta(delta, 3, delta.left.blockByte(), delta.right.blockByte());

            if (delta.left.hasCrc != delta.right.hasCrc) {
                delta.reason = QStringLiteral("One side contains CRC bytes and the other side does not.");
            }

            if (delta.left.bytes.size() != delta.right.bytes.size()) {
                delta.reason = QStringLiteral("Pack byte length differs.");
                const int maxBytes = std::max(delta.left.bytes.size(), delta.right.bytes.size());
                for (int byteOffset = 0; byteOffset < maxBytes; ++byteOffset) {
                    const int leftValue = byteOffset < delta.left.bytes.size()
                        ? static_cast<unsigned char>(delta.left.bytes.at(byteOffset))
                        : -1;
                    const int rightValue = byteOffset < delta.right.bytes.size()
                        ? static_cast<unsigned char>(delta.right.bytes.at(byteOffset))
                        : -1;
                    appendByteDelta(delta, byteOffset, leftValue, rightValue);
                }
            } else if (delta.left.packType() == 0x8F || delta.right.packType() == 0x8F) {
                for (int byteOffset = 0; byteOffset < delta.left.bytes.size(); ++byteOffset) {
                    const int leftValue = static_cast<unsigned char>(delta.left.bytes.at(byteOffset));
                    const int rightValue = static_cast<unsigned char>(delta.right.bytes.at(byteOffset));
                    appendByteDelta(delta, byteOffset, leftValue, rightValue);
                }
            }
        } else {
            const int maxBytes = std::max(delta.left.bytes.size(), delta.right.bytes.size());
            for (int byteOffset = 0; byteOffset < maxBytes; ++byteOffset) {
                const int leftValue = byteOffset < delta.left.bytes.size()
                    ? static_cast<unsigned char>(delta.left.bytes.at(byteOffset))
                    : -1;
                const int rightValue = byteOffset < delta.right.bytes.size()
                    ? static_cast<unsigned char>(delta.right.bytes.at(byteOffset))
                    : -1;
                appendByteDelta(delta, byteOffset, leftValue, rightValue);
            }
            if (delta.left.hasCrc != delta.right.hasCrc) {
                delta.reason = QStringLiteral("One side contains CRC bytes and the other side does not.");
            }
        }

        if (!delta.byteDeltas.isEmpty()) {
            report.identical = false;
            report.packDeltas.append(delta);
        }
    }

    return report;
}

}  // namespace cdmanager::tools::cdtextdiff
