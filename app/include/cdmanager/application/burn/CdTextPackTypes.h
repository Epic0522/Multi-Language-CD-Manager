#pragma once

#include <array>
#include <cstdint>

#include <QString>
#include <QVector>

namespace cdmanager::application::burn {

// CD-TEXT pack type identifiers (MMC-3 Table 602).
inline constexpr std::uint8_t kPackTypeTitle     = 0x80;
inline constexpr std::uint8_t kPackTypePerformer = 0x81;
inline constexpr std::uint8_t kPackTypeSizeInfo  = 0x8F;

// Character codes for the Size Info pack.
inline constexpr std::uint8_t kCharCodeIso88591 = 0x00;
inline constexpr std::uint8_t kCharCodeMsJis    = 0x80;

// Maximum text bytes per pack — the data area is fixed at 12.
inline constexpr int kPackDataSize = 12;
inline constexpr int kPackTotalSize = 18;

// A single 18-byte CD-TEXT pack as defined by the MMC specification.
// Bytes 0–15 carry type, addressing, and text data; bytes 16–17 are CRC-16.
struct CdTextPack {
    std::array<std::uint8_t, kPackTotalSize> data{};

    std::uint8_t packType() const;
    std::uint8_t trackNumber() const;
    std::uint8_t sequenceNumber() const;
    std::uint8_t blockNumber() const;
    std::uint8_t characterPosition() const;
    QString diagnosticString() const;
};

// Container returned by CdTextPackAssembler.
// Owns the complete pack list and provides diagnostic helpers that match
// the style used by CdTextPreviewBuilder.
struct CdTextPackAssembly {
    QVector<CdTextPack> packs;

    int packCount() const;
    int totalByteCount() const;
    QString diagnosticSummary() const;
    QString diagnosticDetail() const;
};

}  // namespace cdmanager::application::burn
