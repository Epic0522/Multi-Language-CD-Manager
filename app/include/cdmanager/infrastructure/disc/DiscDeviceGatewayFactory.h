#pragma once

#include <memory>

#include "cdmanager/infrastructure/disc/DiscDeviceGateway.h"

namespace cdmanager::infrastructure::disc {

// 统一决定当前会话使用哪个光驱网关。
// 规则很简单：优先系统探测，探不到就回退到样本数据，保证界面始终可用。
class DiscDeviceGatewayFactory {
public:
    static std::unique_ptr<DiscDeviceGateway> create();
};

}  // namespace cdmanager::infrastructure::disc
