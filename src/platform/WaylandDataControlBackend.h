#pragma once

#include "ClipboardBackend.h"

#include <QHash>
#include <QStringList>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct ext_data_control_manager_v1;
struct ext_data_control_device_v1;
struct ext_data_control_offer_v1;
struct ext_data_control_source_v1;

class QSocketNotifier;

class WaylandDataControlBackend final : public ClipboardBackend {
    Q_OBJECT
public:
    explicit WaylandDataControlBackend(QObject *parent = nullptr);
    ~WaylandDataControlBackend() override;

    bool start(QString *error = nullptr) override;
    QString backendName() const override;
    bool setClipboard(const MimePayloads &payloads, QString *error = nullptr) override;public:
    // C protocol callbacks must be addressable by the generated listener tables.
    static void registryGlobal(void *, wl_registry *, uint32_t, const char *, uint32_t);
    static void registryGlobalRemove(void *, wl_registry *, uint32_t);
    static void deviceDataOffer(void *, ext_data_control_device_v1 *, ext_data_control_offer_v1 *);
    static void deviceSelection(void *, ext_data_control_device_v1 *, ext_data_control_offer_v1 *);
    static void deviceFinished(void *, ext_data_control_device_v1 *);
    static void devicePrimarySelection(void *, ext_data_control_device_v1 *, ext_data_control_offer_v1 *);
    static void offerMime(void *, ext_data_control_offer_v1 *, const char *);
    static void sourceSend(void *, ext_data_control_source_v1 *, const char *, int32_t);
    static void sourceCancelled(void *, ext_data_control_source_v1 *);

private:
    void captureOffer(ext_data_control_offer_v1 *offer);
    QByteArray receiveMime(ext_data_control_offer_v1 *offer, const QString &mime);
    void destroyOffer(ext_data_control_offer_v1 *offer);
    void onDisplayReadable();

    wl_display *display_ = nullptr;
    wl_registry *registry_ = nullptr;
    wl_seat *seat_ = nullptr;
    ext_data_control_manager_v1 *manager_ = nullptr;
    ext_data_control_device_v1 *device_ = nullptr;
    ext_data_control_offer_v1 *selectionOffer_ = nullptr;
    ext_data_control_offer_v1 *primaryOffer_ = nullptr;
    QHash<ext_data_control_offer_v1 *, QStringList> offers_;
    QHash<ext_data_control_source_v1 *, MimePayloads> sources_;
    QSocketNotifier *notifier_ = nullptr;
    bool suppressOwnSelection_ = false;
};
