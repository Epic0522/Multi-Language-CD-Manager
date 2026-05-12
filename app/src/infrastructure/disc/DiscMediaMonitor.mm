#include "cdmanager/infrastructure/disc/DiscMediaMonitor.h"

#import <CoreFoundation/CoreFoundation.h>
#import <DiskArbitration/DiskArbitration.h>

#include <QMetaObject>

namespace cdmanager::infrastructure::disc {

namespace {

struct MonitorContext {
    DASessionRef session {nullptr};
    DiscMediaMonitor* owner {nullptr};
    bool active {false};
};

bool descriptionBoolValue(CFDictionaryRef description, CFStringRef key) {
    if (description == nullptr || key == nullptr) {
        return false;
    }

    const auto value = static_cast<CFBooleanRef>(CFDictionaryGetValue(description, key));
    return value != nullptr && CFBooleanGetValue(value);
}

bool shouldEmitForDisk(DADiskRef disk) {
    if (disk == nullptr) {
        return false;
    }

    const auto description = DADiskCopyDescription(disk);
    if (description == nullptr) {
        return false;
    }

    // 只关心“整张、可弹出的介质”，避免把桌面卷、分区、挂载点变化也算进去。
    const bool whole = descriptionBoolValue(description, kDADiskDescriptionMediaWholeKey);
    const bool ejectable = descriptionBoolValue(description, kDADiskDescriptionMediaEjectableKey);
    CFRelease(description);
    return whole && ejectable;
}

void emitChange(DiscMediaMonitor* owner) {
    if (owner == nullptr) {
        return;
    }

    QMetaObject::invokeMethod(
        owner,
        [owner]() { emit owner->mediaChanged(); },
        Qt::QueuedConnection
    );
}

void diskAppearedCallback(DADiskRef disk, void* context) {
    if (!shouldEmitForDisk(disk)) {
        return;
    }
    auto* monitor = static_cast<DiscMediaMonitor*>(context);
    emitChange(monitor);
}

void diskDisappearedCallback(DADiskRef disk, void* context) {
    if (!shouldEmitForDisk(disk)) {
        return;
    }
    auto* monitor = static_cast<DiscMediaMonitor*>(context);
    emitChange(monitor);
}

}  // namespace

DiscMediaMonitor::DiscMediaMonitor(QObject* parent)
    : QObject(parent) {
}

DiscMediaMonitor::~DiscMediaMonitor() {
    stop();
}

void DiscMediaMonitor::start() {
    if (isActive()) {
        return;
    }

    auto* context = new MonitorContext;
    context->owner = this;
    context->session = DASessionCreate(kCFAllocatorDefault);
    if (context->session == nullptr) {
        delete context;
        return;
    }

    DARegisterDiskAppearedCallback(context->session, nullptr, diskAppearedCallback, this);
    DARegisterDiskDisappearedCallback(context->session, nullptr, diskDisappearedCallback, this);
    DASessionScheduleWithRunLoop(context->session, CFRunLoopGetMain(), kCFRunLoopCommonModes);
    context->active = true;
    m_implHandle = context;
}

void DiscMediaMonitor::stop() {
    auto* context = static_cast<MonitorContext*>(m_implHandle);
    if (context == nullptr) {
        return;
    }

    if (context->session != nullptr) {
        DAUnregisterCallback(context->session, reinterpret_cast<void*>(diskAppearedCallback), this);
        DAUnregisterCallback(context->session, reinterpret_cast<void*>(diskDisappearedCallback), this);
        DASessionUnscheduleFromRunLoop(context->session, CFRunLoopGetMain(), kCFRunLoopCommonModes);
    }
    if (context->session != nullptr) {
        CFRelease(context->session);
    }

    delete context;
    m_implHandle = nullptr;
}

bool DiscMediaMonitor::isActive() const {
    const auto* context = static_cast<const MonitorContext*>(m_implHandle);
    return context != nullptr && context->active;
}

}  // namespace cdmanager::infrastructure::disc
