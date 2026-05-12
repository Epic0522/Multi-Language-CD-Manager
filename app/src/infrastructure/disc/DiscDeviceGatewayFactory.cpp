#include "cdmanager/infrastructure/disc/DiscDeviceGatewayFactory.h"

#include <QByteArray>
#include <QProcessEnvironment>

#include <memory>

#include "cdmanager/infrastructure/disc/LegacySampleDiscGateway.h"
#include "cdmanager/infrastructure/disc/SystemDiscGateway.h"

namespace cdmanager::infrastructure::disc {

std::unique_ptr<DiscDeviceGateway> DiscDeviceGatewayFactory::create() {
    // 允许通过环境变量强制切换网关，方便之后做真机调试。
    // sample: 总是走样本数据
    // system: 总是走系统探测
    const auto env = QProcessEnvironment::systemEnvironment();
    const QString modeOverride = env.value(QStringLiteral("CDMANAGER_DISC_GATEWAY")).trimmed().toLower();

    if (modeOverride == QStringLiteral("sample")) {
        return std::make_unique<LegacySampleDiscGateway>();
    }

    if (modeOverride == QStringLiteral("system")) {
        return std::make_unique<SystemDiscGateway>();
    }

    auto systemGateway = std::make_unique<SystemDiscGateway>();
    if (!systemGateway->listDrives().isEmpty()) {
        return systemGateway;
    }

    return std::make_unique<LegacySampleDiscGateway>();
}

}  // namespace cdmanager::infrastructure::disc
