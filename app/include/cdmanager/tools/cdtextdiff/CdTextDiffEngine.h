#pragma once

#include "cdmanager/tools/cdtextdiff/CdTextDiffTypes.h"

namespace cdmanager::tools::cdtextdiff {

class CdTextDiffEngine {
public:
    CdTextDiffReport compare(const ParsedCdTextDocument& left,
                             const ParsedCdTextDocument& right,
                             CompareMode mode = CompareMode::Exact) const;
};

}  // namespace cdmanager::tools::cdtextdiff
