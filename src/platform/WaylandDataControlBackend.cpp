#include "WaylandDataControlBackend.h"

#include "ext-data-control-v1-client-protocol.h"

#include <QDebug>
#include <QSocketNotifier>
#include <wayland-client.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace {
constexpr qsizetype kMaxMimeBytes = 64 * 1024 * 1024;
constexpr int kReadTimeoutMs = 2000;

const wl_registry_listener kRegistryListener = {
    WaylandDataControlBackend::registryGlobal,
    WaylandDataControlBackend::registryGlobalRemove,
};

const ext_data_control_device_v1_listener kDeviceListener = {
    WaylandDataControlBackend::deviceDataOffer,
    WaylandDataControlBackend::deviceSelection,
    WaylandDataControlBackend::deviceFinished,
    WaylandDataControlBackend::devicePrimarySelection,
};const ext_data_control_offer_v1_listener kOfferListener = {
    WaylandDataControlBackend::offerMime,
};

const ext_data_control_source_v1_listener kSourceListener = {
    WaylandDataControlBackend::sourceSend,
    WaylandDataControlBackend::sourceCancelled,
};
}

WaylandDataControlBackend::WaylandDataControlBackend(QObject *parent)
    : ClipboardBackend(parent) {}

WaylandDataControlBackend::~WaylandDataControlBackend() {
    if (notifier_) notifier_->setEnabled(false);

    const auto sourceKeys = sources_.keys();
    for (auto *source : sourceKeys)
        ext_data_control_source_v1_destroy(source);
    sources_.clear();

    destroyOffer(selectionOffer_);
    if (primaryOffer_ != selectionOffer_)
        destroyOffer(primaryOffer_);

    if (device_) ext_data_control_device_v1_destroy(device_);
    if (manager_) ext_data_control_manager_v1_destroy(manager_);
    if (seat_) wl_seat_destroy(seat_);
    if (registry_) wl_registry_destroy(registry_);
    if (display_) wl_display_disconnect(display_);
}

QString WaylandDataControlBackend::backendName() const {
    return QStringLiteral("Wayland ext-data-control-v1");
}

bool WaylandDataControlBackend::start(QString *error) {
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        if (error) *error = QStringLiteral("wl_display_connect failed");
        return false;
    }

    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);

    if (wl_display_roundtrip(display_) < 0 || !seat_ || !manager_) {
        if (error) {
            *error = QStringLiteral("Compositor does not expose ext-data-control-v1 or wl_seat");
        }
        return false;
    }

    device_ = ext_data_control_manager_v1_get_data_device(manager_, seat_);
    ext_data_control_device_v1_add_listener(device_, &kDeviceListener, this);

    if (wl_display_roundtrip(display_) < 0) {
        if (error) *error = QStringLiteral("Failed to initialise data-control device");
        return false;
    }

    notifier_ = new QSocketNotifier(wl_display_get_fd(display_),
                                    QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this,
            [this] { onDisplayReadable(); });
    return true;
}

bool WaylandDataControlBackend::setClipboard(const MimePayloads &payloads, QString *error) {
    if (!device_ || !manager_ || payloads.isEmpty()) {
        if (error) *error = QStringLiteral("Clipboard backend is not ready");
        return false;
    }

    auto *source = ext_data_control_manager_v1_create_data_source(manager_);
    if (!source) {
        if (error) *error = QStringLiteral("Failed to create data source");
        return false;
    }

    ext_data_control_source_v1_add_listener(source, &kSourceListener, this);
    for (auto it = payloads.cbegin(); it != payloads.cend(); ++it)
        ext_data_control_source_v1_offer(source, it.key().toUtf8().constData());

    sources_.insert(source, payloads);
    suppressNextSelectionCapture_ = true;
    ext_data_control_device_v1_set_selection(device_, source);

    if (wl_display_flush(display_) < 0 && errno != EAGAIN) {
        sources_.remove(source);
        ext_data_control_source_v1_destroy(source);
        if (error) *error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    return true;
}

void WaylandDataControlBackend::registryGlobal(
    void *data, wl_registry *registry, uint32_t name,
    const char *interface, uint32_t version) {
    auto *self = static_cast<WaylandDataControlBackend *>(data);
    if (std::strcmp(interface, wl_seat_interface.name) == 0 && !self->seat_) {
        self->seat_ = static_cast<wl_seat *>(
            wl_registry_bind(registry, name, &wl_seat_interface,
                             std::min(version, 7u)));
    } else if (std::strcmp(interface, ext_data_control_manager_v1_interface.name) == 0
               && !self->manager_) {
        self->manager_ = static_cast<ext_data_control_manager_v1 *>(
            wl_registry_bind(registry, name,
                             &ext_data_control_manager_v1_interface, 1));
    }
}

void WaylandDataControlBackend::registryGlobalRemove(
    void *, wl_registry *, uint32_t) {}

void WaylandDataControlBackend::deviceDataOffer(
    void *data, ext_data_control_device_v1 *,
    ext_data_control_offer_v1 *offer) {
    auto *self = static_cast<WaylandDataControlBackend *>(data);
    self->offers_.insert(offer, {});
    ext_data_control_offer_v1_add_listener(offer, &kOfferListener, self);
}

void WaylandDataControlBackend::offerMime(
    void *data, ext_data_control_offer_v1 *offer, const char *mime) {
    auto *self = static_cast<WaylandDataControlBackend *>(data);
    if (mime)
        self->offers_[offer].append(QString::fromUtf8(mime));
}

void WaylandDataControlBackend::deviceSelection(
    void *data, ext_data_control_device_v1 *,
    ext_data_control_offer_v1 *offer) {
    auto *self = static_cast<WaylandDataControlBackend *>(data);
    if (self->selectionOffer_ && self->selectionOffer_ != offer)
        self->destroyOffer(self->selectionOffer_);
    self->selectionOffer_ = offer;

    if (!offer) return;
    if (self->suppressNextSelectionCapture_) {
        self->suppressNextSelectionCapture_ = false;
        return;
    }
    self->captureOffer(offer);
}

void WaylandDataControlBackend::devicePrimarySelection(
    void *data, ext_data_control_device_v1 *,
    ext_data_control_offer_v1 *offer) {
    auto *self = static_cast<WaylandDataControlBackend *>(data);
    if (self->primaryOffer_ && self->primaryOffer_ != offer)
        self->destroyOffer(self->primaryOffer_);
    self->primaryOffer_ = offer;
}

void WaylandDataControlBackend::deviceFinished(
    void *data, ext_data_control_device_v1 *) {
    auto *self = static_cast<WaylandDataControlBackend *>(data);
    emit self->backendError(QStringLiteral("Wayland data-control device finished"));
}

void WaylandDataControlBackend::captureOffer(ext_data_control_offer_v1 *offer) {
    MimePayloads payloads;
    const QStringList mimes = offers_.value(offer);
    for (const QString &mime : mimes) {
        if (mime.isEmpty()) continue;
        QByteArray bytes = receiveMime(offer, mime);
        if (!bytes.isNull())
            payloads.insert(mime, std::move(bytes));
    }

    if (!payloads.isEmpty()) {
        qInfo() << "Captured clipboard:" << payloads.keys();
        emit clipboardCaptured(payloads);
    }
}

QByteArray WaylandDataControlBackend::receiveMime(
    ext_data_control_offer_v1 *offer, const QString &mime) {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0)
        return {};

    const QByteArray mimeUtf8 = mime.toUtf8();
    ext_data_control_offer_v1_receive(offer, mimeUtf8.constData(), fds[1]);
    close(fds[1]);

    if (wl_display_flush(display_) < 0 && errno != EAGAIN) {
        close(fds[0]);
        return {};
    }

    QByteArray out;
    char buffer[64 * 1024];
    pollfd pfd{fds[0], POLLIN | POLLHUP, 0};    bool done = false;
    while (!done && out.size() <= kMaxMimeBytes) {
        const int rc = poll(&pfd, 1, kReadTimeoutMs);
        if (rc <= 0) break;

        while (true) {
            const ssize_t n = read(fds[0], buffer, sizeof(buffer));
            if (n > 0) {
                out.append(buffer, n);
                if (out.size() > kMaxMimeBytes) {
                    out.clear();
                    done = true;
                    break;
                }
            } else if (n == 0) {
                done = true;
                break;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                out.clear();
                done = true;
                break;
            }
        }
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))
            done = true;
    }
    close(fds[0]);
    return out;
}

void WaylandDataControlBackend::destroyOffer(ext_data_control_offer_v1 *offer) {
    if (!offer) return;
    offers_.remove(offer);
    ext_data_control_offer_v1_destroy(offer);
    if (selectionOffer_ == offer) selectionOffer_ = nullptr;
    if (primaryOffer_ == offer) primaryOffer_ = nullptr;
}

void WaylandDataControlBackend::sourceSend(
    void *data, ext_data_control_source_v1 *source,
    const char *mimeType, int32_t fd) {
    auto *self = static_cast<WaylandDataControlBackend *>(data);
    const MimePayloads payloads = self->sources_.value(source);
    const QByteArray bytes = payloads.value(QString::fromUtf8(mimeType));

    qsizetype offset = 0;
    while (offset < bytes.size()) {
        const ssize_t n = write(fd, bytes.constData() + offset,
                                static_cast<size_t>(bytes.size() - offset));
        if (n > 0) {
            offset += n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
    close(fd);
}

void WaylandDataControlBackend::sourceCancelled(
    void *data, ext_data_control_source_v1 *source) {
    auto *self = static_cast<WaylandDataControlBackend *>(data);
    self->sources_.remove(source);
    ext_data_control_source_v1_destroy(source);
}

void WaylandDataControlBackend::onDisplayReadable() {
    if (wl_display_dispatch(display_) < 0) {
        notifier_->setEnabled(false);
        emit backendError(QStringLiteral("Wayland display connection failed"));
        return;
    }
    wl_display_flush(display_);
}