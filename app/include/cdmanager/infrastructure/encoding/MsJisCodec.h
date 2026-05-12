#pragma once

#include <QString>

#include "cdmanager/application/ValidationTypes.h"

namespace cdmanager::infrastructure::encoding {

class MsJisCodec {
public:
    cdmanager::application::EncodedText encode(const QString& text) const;
};

}  // namespace cdmanager::infrastructure::encoding
