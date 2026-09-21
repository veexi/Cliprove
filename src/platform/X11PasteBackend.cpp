#include "X11PasteBackend.h"

#include <X11/keysym.h>
#include <cstdlib>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xtest.h>

X11PasteBackend::X11PasteBackend(QObject *parent)
    : PasteBackend(parent) {}

X11PasteBackend::~X11PasteBackend() {
    if (connection_)
        xcb_disconnect(connection_);
}

QString X11PasteBackend::backendName() const {
    return QStringLiteral("X11 XTest");
}

bool X11PasteBackend::isReady() const {
    return ready_;
}

bool X11PasteBackend::start(QString *error) {
    int screen = 0;
    connection_ = xcb_connect(nullptr, &screen);
    if (!connection_ || xcb_connection_has_error(connection_)) {
        if (error) *error = QStringLiteral("Unable to connect to X11");
        return false;
    }

    auto *symbols = xcb_key_symbols_alloc(connection_);
    if (!symbols) {
        if (error) *error = QStringLiteral("xcb_key_symbols_alloc failed");
        return false;
    }

    xcb_keycode_t *ctrlCodes =
        xcb_key_symbols_get_keycode(symbols, XK_Control_L);
    xcb_keycode_t *vCodes =
        xcb_key_symbols_get_keycode(symbols, XK_v);

    if (ctrlCodes && ctrlCodes[0]) ctrlKeycode_ = ctrlCodes[0];
    if (vCodes && vCodes[0]) vKeycode_ = vCodes[0];

    std::free(ctrlCodes);
    std::free(vCodes);
    xcb_key_symbols_free(symbols);

    if (!ctrlKeycode_ || !vKeycode_) {
        if (error) *error = QStringLiteral("Unable to resolve Ctrl/V keycodes");
        return false;
    }

    ready_ = true;
    emit readyChanged(true);
    emit statusChanged(QStringLiteral("Direct paste ready"));
    return true;
}bool X11PasteBackend::paste(QString *error) {
    if (!ready_ || !connection_) {
        if (error) *error = QStringLiteral("X11 paste backend is not ready");
        return false;
    }

    const auto send = [this](uint8_t type, uint8_t keycode) {
        xcb_test_fake_input(connection_, type, keycode,
                            XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
    };

    send(XCB_KEY_PRESS, ctrlKeycode_);
    send(XCB_KEY_PRESS, vKeycode_);
    send(XCB_KEY_RELEASE, vKeycode_);
    send(XCB_KEY_RELEASE, ctrlKeycode_);
    xcb_flush(connection_);

    emit statusChanged(QStringLiteral("Ctrl+V injected"));
    return true;
}