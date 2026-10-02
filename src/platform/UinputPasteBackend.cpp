/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "UinputPasteBackend.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/uinput.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

UinputPasteBackend::UinputPasteBackend(QObject *parent) : PasteBackend(parent) {
    settleTimer_.setSingleShot(true);
    connect(&settleTimer_, &QTimer::timeout, this, [this] {
        ready_ = true;
        emit readyChanged(true);
        emit statusChanged(QStringLiteral("Direct paste ready (uinput)"));
    });
}

UinputPasteBackend::~UinputPasteBackend() {
    resetDevice();
}

void UinputPasteBackend::resetDevice() {
    settleTimer_.stop();
    const bool wasReady = ready_;
    ready_ = false;
    if (fd_ >= 0) {
        if (created_) {
            // Best effort even after a failed write: never leave a modifier down.
            key(KEY_V, false);
            key(KEY_LEFTSHIFT, false);
            key(KEY_LEFTCTRL, false);
            ioctl(fd_, UI_DEV_DESTROY);
        }
        close(fd_);
    }
    fd_ = -1;
    created_ = false;
    if (wasReady) emit readyChanged(false);
}

bool UinputPasteBackend::start(QString *error) {
    if (fd_ >= 0) return true;
    auto fail = [this, error](const QString &operation) {
        const QString message = operation + QStringLiteral(": ")
            + QString::fromLocal8Bit(std::strerror(errno));
        resetDevice();
        if (error) *error = message;
        emit statusChanged(message);
        return false;
    };
    fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) return fail(QStringLiteral("Cannot open /dev/uinput"));
    if (ioctl(fd_, UI_SET_EVBIT, EV_KEY) < 0)
        return fail(QStringLiteral("Cannot enable keyboard events"));
    // Advertise a standard keyboard so udev/libinput classify it as a keyboard.
    // Only the fixed paste chords below are exposed by our D-Bus service.
    for (int code = KEY_ESC; code <= KEY_KPDOT; ++code) {
        if (ioctl(fd_, UI_SET_KEYBIT, code) < 0)
            return fail(QStringLiteral("Cannot configure keyboard keys"));
    }
    uinput_setup setup{};
    setup.id.bustype = BUS_VIRTUAL;
    std::strncpy(setup.name, "Cliprove paste keyboard", UINPUT_MAX_NAME_SIZE - 1);
    if (ioctl(fd_, UI_DEV_SETUP, &setup) < 0)
        return fail(QStringLiteral("Cannot configure virtual keyboard"));
    if (ioctl(fd_, UI_DEV_CREATE) < 0)
        return fail(QStringLiteral("Cannot create virtual keyboard"));
    created_ = true;
    emit statusChanged(QStringLiteral("Initializing virtual paste keyboard..."));
    // Device discovery by udev and the compositor is asynchronous.
    settleTimer_.start(750);
    return true;
}

bool UinputPasteBackend::writeEvent(unsigned short type, unsigned short code, int value) {
    input_event event{};
    event.type = type;
    event.code = code;
    event.value = value;
    ssize_t count;
    do {
        count = write(fd_, &event, sizeof(event));
    } while (count < 0 && errno == EINTR);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        pollfd writable{fd_, POLLOUT, 0};
        int result;
        do {
            result = poll(&writable, 1, 100);
        } while (result < 0 && errno == EINTR);
        if (result <= 0) {
            if (result == 0) errno = ETIMEDOUT;
            return false;
        }
        do {
            count = write(fd_, &event, sizeof(event));
        } while (count < 0 && errno == EINTR);
    }
    if (count == sizeof(event)) return true;
    if (count >= 0) errno = EIO;
    return false;
}

bool UinputPasteBackend::key(unsigned short code, bool pressed) {
    return writeEvent(EV_KEY, code, pressed) && writeEvent(EV_SYN, SYN_REPORT, 0);
}

bool UinputPasteBackend::paste(QString *error) {
    return pasteWithShift(false, error);
}

bool UinputPasteBackend::pasteWithShift(bool shift, QString *error) {
    if (!ready_) {
        if (error) *error = QStringLiteral("Virtual paste keyboard is not ready");
        return false;
    }
    const bool ok = key(KEY_LEFTCTRL, true)
        && (!shift || key(KEY_LEFTSHIFT, true))
        && key(KEY_V, true) && key(KEY_V, false)
        && (!shift || key(KEY_LEFTSHIFT, false))
        && key(KEY_LEFTCTRL, false);
    if (!ok) {
        const QString message = QStringLiteral("Virtual keyboard write failed: ")
            + QString::fromLocal8Bit(std::strerror(errno));
        resetDevice();
        if (error) *error = message;
        emit statusChanged(message);
        return false;
    }
    emit statusChanged(shift ? QStringLiteral("Ctrl+Shift+V injected (uinput)")
                             : QStringLiteral("Ctrl+V injected (uinput)"));
    return true;
}
