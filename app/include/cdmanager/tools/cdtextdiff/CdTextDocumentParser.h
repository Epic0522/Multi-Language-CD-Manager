#pragma once

#include "cdmanager/tools/cdtextdiff/CdTextDiffTypes.h"

namespace cdmanager::tools::cdtextdiff {

struct ParseResult {
    bool ok {false};
    ParsedCdTextDocument document;
    QString errorMessage;
};

class CdTextDocumentParser {
public:
    ParseResult parseFile(const QString& path, InputFormat format) const;
    ParseResult parseBytes(const QByteArray& bytes, InputFormat format, const QString& sourceLabel) const;

private:
    ParseResult parseCdt(const QByteArray& bytes, const QString& sourceLabel) const;
    ParseResult parsePacksJson(const QByteArray& bytes, const QString& sourceLabel) const;
    ParseResult parseSampleDump(const QByteArray& bytes, const QString& sourceLabel) const;
    ParseResult parseReferenceSample(const QString& path) const;
};

}  // namespace cdmanager::tools::cdtextdiff
