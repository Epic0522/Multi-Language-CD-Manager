#include "cdmanager/presentation/ui/MacVisualEffectHelper.h"

#ifdef Q_OS_MACOS

#import <AppKit/AppKit.h>

#include <QWidget>

namespace cdmanager::presentation::ui {

void applyMacVisualEffect(QWidget* widget, bool darkMode) {
    if (widget == nullptr) {
        return;
    }

    widget->setAttribute(Qt::WA_TranslucentBackground, true);
    widget->setAutoFillBackground(false);
    NSView* hostView = reinterpret_cast<NSView*>(widget->winId());
    if (hostView == nil) {
        return;
    }

    hostView.wantsLayer = YES;
    hostView.layer.backgroundColor = NSColor.clearColor.CGColor;
    hostView.layer.opaque = NO;

    NSWindow* window = hostView.window;
    if (window != nil) {
        window.opaque = NO;
        window.backgroundColor = NSColor.clearColor;
        window.hasShadow = YES;
        window.titlebarAppearsTransparent = YES;
    }
    if (window == nil) {
        return;
    }

    constexpr NSInteger kEffectTag = 424242;
    NSView* currentContentView = window.contentView;
    NSVisualEffectView* effectView = nil;

    if ([currentContentView isKindOfClass:[NSVisualEffectView class]] &&
        currentContentView.tag == kEffectTag) {
        effectView = (NSVisualEffectView*)currentContentView;
    } else {
        currentContentView.wantsLayer = YES;
        currentContentView.layer.backgroundColor = NSColor.clearColor.CGColor;
        currentContentView.layer.opaque = NO;

        effectView = [[NSVisualEffectView alloc] initWithFrame:currentContentView.bounds];
        effectView.tag = kEffectTag;
        effectView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        effectView.blendingMode = NSVisualEffectBlendingModeBehindWindow;

        const bool isFloatingWindow = widget->window() == widget;
        effectView.material = isFloatingWindow
            ? NSVisualEffectMaterialHUDWindow
            : NSVisualEffectMaterialUnderWindowBackground;

        window.contentView = effectView;
        [effectView addSubview:currentContentView];
        currentContentView.frame = effectView.bounds;
        currentContentView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    }

    effectView.state = NSVisualEffectStateActive;
    effectView.wantsLayer = YES;
    effectView.layer.backgroundColor = NSColor.clearColor.CGColor;
    effectView.appearance = [NSAppearance appearanceNamed:
        darkMode ? NSAppearanceNameVibrantDark : NSAppearanceNameVibrantLight];
    effectView.alphaValue = 1.0;
}

void configureMacWindowChrome(QWidget* widget) {
    if (widget == nullptr) {
        return;
    }

    NSView* hostView = reinterpret_cast<NSView*>(widget->winId());
    if (hostView == nil || hostView.window == nil) {
        return;
    }

    NSWindow* window = hostView.window;
    window.titleVisibility = NSWindowTitleHidden;
    window.titlebarAppearsTransparent = YES;
    window.movableByWindowBackground = YES;
    window.opaque = NO;
    window.backgroundColor = NSColor.clearColor;
    window.styleMask = window.styleMask | NSWindowStyleMaskFullSizeContentView;
}

}  // namespace cdmanager::presentation::ui

#else

#include <QWidget>

namespace cdmanager::presentation::ui {

void applyMacVisualEffect(QWidget* widget, bool darkMode) {
    Q_UNUSED(widget)
    Q_UNUSED(darkMode)
}

void configureMacWindowChrome(QWidget* widget) {
    Q_UNUSED(widget)
}

}  // namespace cdmanager::presentation::ui

#endif
