/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "PasteController.h"

#include <QAbstractItemModel>
#include <QClipboard>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>
#include <QFile>
#include <QImage>
#include <QUrl>
#include <QPointer>
#include <QSet>
#include <QTimer>

namespace {
QDBusMessage call(const QString &method) {
    return QDBusMessage::createMethodCall(QStringLiteral("org.veexi.CliprovePaste"),
        QStringLiteral("/Paste"), QStringLiteral("org.veexi.CliprovePaste"), method);
}
}

bool PasteController::usesShift(const QString &appId) {
    const QString id = appId.toLower();
    static const QSet<QString> terminals{
        QStringLiteral("konsole"), QStringLiteral("org.kde.konsole"),
        QStringLiteral("org.gnome.terminal"), QStringLiteral("gnome-terminal-server"),
        QStringLiteral("gnome-terminal"), QStringLiteral("org.gnome.console"),
        QStringLiteral("kgx"), QStringLiteral("kitty"), QStringLiteral("alacritty"),
        QStringLiteral("org.wezfurlong.wezterm"), QStringLiteral("wezterm"),
        QStringLiteral("xfce4-terminal"), QStringLiteral("terminator"),
        QStringLiteral("tilix"), QStringLiteral("com.gexperts.tilix"),
        QStringLiteral("foot"), QStringLiteral("footclient"),
        QStringLiteral("com.mitchellh.ghostty"), QStringLiteral("ghostty")};
    return terminals.contains(id);
}

void PasteController::cancel() {
    ++generation_;
    busy_ = false;
    queue_.clear();
    queueIndex_ = 0;
    readArmed_ = false;
    emit stateChanged();
}

void PasteController::fail(const QString &message) {
    error_ = message;
    cancel();
    emit failed();
}

void PasteController::paste(QObject *history, const QVariantList &uuids, bool shift) {
    if (busy_ || uuids.isEmpty()) return;
    // Shift+click / Shift+Enter also covers embedded terminals whose window
    // app ID belongs to an editor or a remote desktop client.
    shift = shift || (QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier);
    auto *model = qobject_cast<QAbstractItemModel *>(history);
    if (!model) {
        fail(QStringLiteral("无法读取剪贴板历史。"));
        return;
    }
    const auto roles = model->roleNames();
    const int uuidRole = roles.key("uuid", -1);
    const int typeRole = roles.key("type", -1);
    QSet<QString> wanted;
    for (const auto &uuid : uuids) wanted.insert(uuid.toString());
    QStringList texts;
    QString singleUuid;
    QList<MimePayloads> payloads;
    bool queueSupported = true;
    bool allText = true;
    int found = 0;
    // Resolve stable UUIDs at execution time; old delegate row numbers can move.
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        const QString uuid = model->data(index, uuidRole).toString();
        if (!wanted.contains(uuid)) continue;
        ++found;
        singleUuid = uuid;
        const int type = model->data(index, typeRole).toInt();
        const QString text = model->data(index, Qt::DisplayRole).toString();
        allText &= type == 2;
        texts.append(text);
        if (type == 2) {
            payloads.append({{QStringLiteral("text/plain;charset=utf-8"), text.toUtf8()},
                             {QStringLiteral("text/plain"), text.toUtf8()}});
        } else if (type == 4) {
            const QUrl imageUrl = model->data(index, roles.key("decoration", -1)).toUrl();
            QFile imageFile(imageUrl.toLocalFile());
            if (!imageUrl.isLocalFile() || !imageFile.open(QIODevice::ReadOnly)) {
                fail(QStringLiteral("无法读取所选图片，请重新复制后重试。"));
                return;
            }
            const QByteArray png = imageFile.readAll();
            if (QImage::fromData(png, "PNG").isNull()) {
                fail(QStringLiteral("图片记录已损坏或格式不可用。"));
                return;
            }
            payloads.append({{QStringLiteral("image/png"), png}});
        } else {
            queueSupported = false;
        }
    }
    if (found != wanted.size() || uuidRole < 0 || typeRole < 0) {
        fail(QStringLiteral("部分条目已不存在，请重新选择。"));
        return;
    }
    if (found > 1 && !allText && (!queueSupported || !publishPayload)) {
        fail(QStringLiteral("此组合无法批量粘贴；文件条目请单独粘贴。"));
        return;
    }
    // Texts form one paste. Images remain independent offers and are sent in
    // order; never flatten them into one bitmap or repeatedly paste the last.
    queue_.clear();
    queueIndex_ = 0;
    if (!allText && queueSupported && publishPayload) queue_ = payloads;

    error_.clear();
    busy_ = true;
    const quint64 request = ++generation_;
    emit stateChanged();
    QPointer<QAbstractItemModel> modelGuard(model);
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(call(QStringLiteral("ensureReady")), 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, request, modelGuard, singleUuid, texts, found, allText, shift] {
        QDBusPendingReply<bool> reply = *watcher;
        watcher->deleteLater();
        if (request != generation_) return;
        if (reply.isError()) {
            fail(QStringLiteral("无法连接自动粘贴服务：%1").arg(reply.error().message()));
            return;
        }
        if (!reply.value()) {
            fail(QStringLiteral("自动粘贴尚未就绪。若出现键盘控制授权，请允许后再次点击条目。"));
            return;
        }
        if (!modelGuard) {
            fail(QStringLiteral("剪贴板历史已关闭，请重新打开。"));
            return;
        }
        if (!queue_.isEmpty()) {
            if (!publishCurrent()) return;
        } else if (found > 1) {
            QGuiApplication::clipboard()->setText(texts.join(QLatin1Char('\n')));
        } else if (!QMetaObject::invokeMethod(modelGuard, "moveToTop", Q_ARG(QString, singleUuid))) {
            fail(QStringLiteral("无法恢复所选条目。"));
            return;
        }
        // Give Klipper time to publish its selection before returning focus.
        QTimer::singleShot(180, this, [this, request, shift, found, allText, texts] {
            if (request != generation_) return;
            // Klipper 6.4 does not republish row zero when it only came from
            // PRIMARY selection. In that case restore the selected text, too.
            if (found == 1 && allText && QGuiApplication::clipboard()->text() != texts.front())
                QGuiApplication::clipboard()->setText(texts.front());
            hideWhenReleased(request, shift);
        });
    });
}

void PasteController::hideWhenReleased(quint64 request, bool shift, int attempts) {
    if (request != generation_) return;
    // On Wayland key releases stop arriving after the popup loses focus.
    // Wait while it still owns keyboard focus, otherwise a cached Ctrl/Meta
    // state can outlive the real key press or alter the injected shortcut.
    if (QGuiApplication::queryKeyboardModifiers() != Qt::NoModifier) {
        if (attempts >= 125) {
            fail(QStringLiteral("请先松开 Ctrl、Shift、Alt 或 Win 键，再点击粘贴。"));
            return;
        }
        QTimer::singleShot(40, this, [this, request, shift, attempts] {
            hideWhenReleased(request, shift, attempts + 1);
        });
        return;
    }
    emit hideRequested();
    QTimer::singleShot(80, this, [this, request, shift] {
        injectWhenFocused(request, shift);
    });
}

void PasteController::injectWhenFocused(quint64 request, bool shift, int attempts) {
    if (request != generation_) return;
    if (targetIsActive && !targetIsActive()) {
        if (attempts >= 50) {
            fail(QStringLiteral("无法回到原窗口，或快捷键尚未松开；请重新打开剪贴板后重试。"));
            return;
        }
        QTimer::singleShot(40, this, [this, request, shift, attempts] {
            injectWhenFocused(request, shift, attempts + 1);
        });
        return;
    }
    auto message = call(QStringLiteral("pasteWithShift"));
    // Terminal applications such as Codex use Ctrl+V for image input, while
    // text must go through the terminal's Ctrl+Shift+V bracketed paste.
    const bool image = !queue_.isEmpty() && queue_[queueIndex_].contains(QStringLiteral("image/png"));
    message << (shift && !image);
    readArmed_ = !queue_.isEmpty();
    payloadRead_ = false;
    auto *watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(message, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request, shift] {
        QDBusPendingReply<bool> reply = *watcher;
        watcher->deleteLater();
        if (request != generation_) return;
        if (reply.isError() || !reply.value()) {
            fail(QStringLiteral("自动粘贴失败，内容已复制，可手动粘贴或重试。"));
            return;
        }
        if (!queue_.isEmpty()) {
            // Clipboard requests can complete after the key-injection reply.
            // Keep each payload stable until it has been read, with additional
            // time for applications to create the attachment before the next.
            QTimer::singleShot(800, this, [this, request, shift] { advanceQueue(request, shift); });
            return;
        }
        busy_ = false;
        emit stateChanged();
        emit completed();
    });
}

bool PasteController::publishCurrent() {
    readArmed_ = false;
    payloadRead_ = false;
    QString error;
    if (!publishPayload(queue_[queueIndex_], &error)) {
        fail(QStringLiteral("无法恢复第 %1 项：%2").arg(queueIndex_ + 1).arg(error));
        return false;
    }
    return true;
}

void PasteController::clipboardRead(const QString &mime) {
    if (busy_ && readArmed_ && !queue_.isEmpty() && queue_[queueIndex_].contains(mime))
        payloadRead_ = true;
}

void PasteController::advanceQueue(quint64 request, bool shift, int attempts) {
    if (request != generation_) return;
    if (!payloadRead_) {
        if (attempts >= 50) {
            fail(QStringLiteral("目标应用未读取第 %1 项，已停止剩余粘贴；请确认输入框支持此内容。")
                 .arg(queueIndex_ + 1));
            return;
        }
        QTimer::singleShot(100, this, [this, request, shift, attempts] {
            advanceQueue(request, shift, attempts + 1);
        });
        return;
    }
    if (++queueIndex_ >= queue_.size()) {
        queue_.clear();
        readArmed_ = false;
        busy_ = false;
        emit stateChanged();
        emit completed();
        return;
    }
    if (targetIsActive && !targetIsActive()) {
        fail(QStringLiteral("目标窗口已切换，已停止剩余粘贴。"));
        return;
    }
    if (!publishCurrent()) return;
    QTimer::singleShot(180, this, [this, request, shift] { injectWhenFocused(request, shift); });
}
