#pragma once

#include "cdmanager/application/CdTextWritePayloadBuilder.h"
#include "cdmanager/application/burn/CdTextPackTypes.h"

namespace cdmanager::application::burn {

// Converts a CdTextWritePayload into MMC CD-TEXT binary packs.
//
// This is pure data transformation — no system calls, no library
// dependencies.  The output CdTextPackAssembly can be handed directly to a
// future infrastructure::burn adapter that wraps libburn.
//
// Multi-language projects produce separate blocks per character encoding;
// the Latin block (ISO-8859-1) is emitted before the Japanese block
// (MS-JIS).
class CdTextPackAssembler {
public:
    CdTextPackAssembly assemble(const CdTextWritePayload& payload) const;
};

}  // namespace cdmanager::application::burn
