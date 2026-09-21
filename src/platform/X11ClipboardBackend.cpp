#include "X11ClipboardBackend.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

X11ClipboardBackend::X11ClipboardBackend(QObject *parent)
    : ClipboardBackend(parent) {}

QString X11ClipboardBackend::backendName() const {
    return QStringLiteral("X11 Qt clipboard");
}

bool X11ClipboardBackend::start(QString *error) {
    clipboard_ = QGuiApplication::clipboard();
    if (!clipboard_) {
        if (error) *error = QStringLiteral("QClipboard is unavailable");
        return false;
    }

    connect(clipboard_, &QClipboard::dataChanged,
            this, &X11ClipboardBackend::captureClipboard);
    captureClipboard();
    return true;
}

void X11ClipboardBackend::captureClipboard() {
    if (!clipboard_) return;
    if (suppressNextCapture_) {
        suppressNextCapture_ = false;
        return;
    }

    const QMimeData *mime = clipboard_->mimeData(QClipboard::Clipboard);
    if (!mime) return;

    MimePayloads payloads;
    for (const QString &format : mime->formats())
        payloads.insert(format, mime->data(format));

    if (!payloads.isEmpty())
        emit clipboardCaptured(payloads);
}

bool X11ClipboardBackend::setClipboard(
    const MimePayloads &payloads, QString *error) {
    if (!clipboard_ || payloads.isEmpty()) {
        if (error) *error = QStringLiteral("Clipboard backend is not ready");
        return false;
    }

    auto *mime = new QMimeData;
    for (auto it = payloads.cbegin(); it != payloads.cend(); ++it)
        mime->setData(it.key(), it.value());

    suppressNextCapture_ = true;
    clipboard_->setMimeData(mime, QClipboard::Clipboard);
    return true;
}