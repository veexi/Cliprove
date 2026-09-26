/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "popup/PasteController.h"
#include <QAbstractListModel>
#include <QClipboard>
#include <QDBusConnection>
#include <QGuiApplication>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>
#include <QImage>
#include <QTemporaryDir>
#include <QUrl>

class TestHistory : public QAbstractListModel {
    Q_OBJECT
public:
    struct Entry { QString uuid; QString text; int type = 2; QUrl imageUrl; };
    QList<Entry> entries{{"a", "第一段\n保留换行"}, {"b", QString(2000, QChar('x'))}, {"c", "末尾 "}};
    int moves = 0;
    bool publish = true;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : entries.size(); }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::DisplayRole, "display"}, {256, "uuid"}, {257, "type"}, {258, "decoration"}}; }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid()) return {};
        const auto &entry = entries[index.row()];
        if (role == 256) return entry.uuid;
        if (role == 257) return entry.type;
        if (role == 258) return entry.imageUrl;
        return entry.text;
    }
    Q_INVOKABLE void moveToTop(const QString &uuid) {
        ++moves;
        if (!publish) return;
        for (const auto &entry : entries) if (entry.uuid == uuid) {
            auto *mime = new QMimeData;
            mime->setText(entry.text);
            mime->setHtml("<b>preserved</b>");
            QGuiApplication::clipboard()->setMimeData(mime);
            break;
        }
    }
};

class TestPasteService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.veexi.CliprovePaste")
public:
    bool available = true;
    bool result = true;
    int count = 0;
    bool shift = false;
    QString text;
    QString html;
    QList<QByteArray> images;
    QList<bool> shifts;
    std::function<void()> afterPaste;
public slots:
    bool ensureReady() { return available; }
    bool pasteWithShift(bool useShift) {
        ++count;
        shift = useShift;
        text = QGuiApplication::clipboard()->text();
        html = QGuiApplication::clipboard()->mimeData()->html();
        images.append(QGuiApplication::clipboard()->mimeData()->data("image/png"));
        shifts.append(useShift);
        if (afterPaste) afterPaste();
        return result;
    }
};

class PasteControllerTest : public QObject {
    Q_OBJECT
    TestPasteService service;
private slots:
    void initTestCase() {
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService("org.veexi.CliprovePaste"));
        QVERIFY(bus.registerObject("/Paste", &service, QDBusConnection::ExportAllSlots));
    }
    void init() {
        service.available = service.result = true;
        service.count = 0;
        service.text.clear();
        service.html.clear();
        service.images.clear();
        service.shifts.clear();
        service.afterPaste = {};
        QGuiApplication::clipboard()->setText("unchanged");
    }
    void multiTextIsOnePasteInListOrder() {
        TestHistory model;
        PasteController controller;
        QSignalSpy done(&controller, &PasteController::completed);
        controller.paste(&model, {"c", "a", "b"}, false);
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(service.count, 1);
        QCOMPARE(model.moves, 0);
        QCOMPARE(service.text, model.entries[0].text + '\n' + model.entries[1].text + '\n' + model.entries[2].text);
        QVERIFY(!service.shift);
    }
    void singleItemPreservesMimeAndUsesTerminalShortcut() {
        TestHistory model;
        PasteController controller;
        QSignalSpy done(&controller, &PasteController::completed);
        controller.paste(&model, {"b"}, PasteController::usesShift("org.kde.konsole"));
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(service.count, 1);
        QCOMPARE(model.moves, 1);
        QCOMPARE(service.text, model.entries[1].text);
        QCOMPARE(service.html, QString("<b>preserved</b>"));
        QVERIFY(service.shift);
    }
    void unavailableDoesNotReplaceClipboardOrHide() {
        service.available = false;
        TestHistory model;
        PasteController controller;
        QSignalSpy failed(&controller, &PasteController::failed);
        QSignalSpy hidden(&controller, &PasteController::hideRequested);
        controller.paste(&model, {"a", "b"}, false);
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(hidden.count(), 0);
        QCOMPARE(service.count, 0);
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("unchanged"));
        QVERIFY(!controller.error().isEmpty());
    }
    void topItemFromPrimarySelectionStillRestoresClipboard() {
        TestHistory model;
        model.publish = false;
        PasteController controller;
        QSignalSpy done(&controller, &PasteController::completed);
        controller.paste(&model, {"a"}, false);
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(service.text, model.entries[0].text);
    }
    void missingOrMixedItemsDoNotPartiallyPaste() {
        TestHistory model;
        PasteController controller;
        controller.paste(&model, {"a", "missing"}, false);
        QVERIFY(!controller.error().isEmpty());
        model.entries[1].type = 4;
        controller.paste(&model, {"a", "b"}, false);
        QVERIFY(!controller.error().isEmpty());
        QCOMPARE(service.count, 0);
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("unchanged"));
    }
    void focusMustReturnBeforeInjection() {
        TestHistory model;
        PasteController controller;
        bool active = false;
        controller.targetIsActive = [&] { return active; };
        QSignalSpy hidden(&controller, &PasteController::hideRequested);
        QSignalSpy done(&controller, &PasteController::completed);
        controller.paste(&model, {"a"}, false);
        QTRY_COMPARE(hidden.count(), 1);
        QTest::qWait(250);
        QCOMPARE(service.count, 0);
        active = true;
        QTRY_COMPARE(done.count(), 1);
        QCOMPARE(service.count, 1);
    }
    void reopeningCancelsPendingPaste() {
        TestHistory model;
        PasteController controller;
        controller.paste(&model, {"a"}, false);
        controller.cancel();
        QTest::qWait(400);
        QCOMPARE(service.count, 0);
        QCOMPARE(model.moves, 0);
    }
    void lostTargetAbortsWithoutInjection() {
        TestHistory model;
        PasteController controller;
        controller.targetIsActive = [] { return false; };
        QSignalSpy failed(&controller, &PasteController::failed);
        controller.paste(&model, {"a"}, false);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 4000);
        QCOMPARE(service.count, 0);
    }
    void doubleClickDoesNotPasteTwice() {
        TestHistory model;
        PasteController controller;
        QSignalSpy done(&controller, &PasteController::completed);
        controller.paste(&model, {"a"}, false);
        controller.paste(&model, {"b"}, false);
        QTRY_COMPARE(done.count(), 1);
        QTest::qWait(300);
        QCOMPARE(service.count, 1);
        QCOMPARE(service.text, model.entries[0].text);
    }
    void injectionFailureIsVisible() {
        service.result = false;
        TestHistory model;
        PasteController controller;
        QSignalSpy failed(&controller, &PasteController::failed);
        controller.paste(&model, {"a"}, false);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!controller.error().isEmpty());
        QVERIFY(!controller.busy());
    }
    void terminalDetectionDoesNotMatchTitlesOrEditors() {
        QVERIFY(PasteController::usesShift("konsole"));
        QVERIFY(PasteController::usesShift("Alacritty"));
        QVERIFY(!PasteController::usesShift("org.kde.kate"));
        QVERIFY(!PasteController::usesShift("code"));
        QVERIFY(!PasteController::usesShift("firefox"));
        QVERIFY(!PasteController::usesShift("konsole-notes.txt"));
    }
    void imagesAreSeparateAndWaitForRead() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QImage red(16, 16, QImage::Format_RGB32), blue(16, 16, QImage::Format_RGB32);
        red.fill(Qt::red);
        blue.fill(Qt::blue);
        QVERIFY(red.save(directory.filePath("red.png")));
        QVERIFY(blue.save(directory.filePath("blue.png")));
        TestHistory model;
        model.entries = {{"red", "red", 4, QUrl::fromLocalFile(directory.filePath("red.png"))},
                         {"blue", "blue", 4, QUrl::fromLocalFile(directory.filePath("blue.png"))}};
        PasteController controller;
        controller.publishPayload = [](const MimePayloads &payload, QString *) {
            auto *mime = new QMimeData;
            for (auto it = payload.cbegin(); it != payload.cend(); ++it) mime->setData(it.key(), it.value());
            QGuiApplication::clipboard()->setMimeData(mime);
            return true;
        };
        QSignalSpy done(&controller, &PasteController::completed);
        controller.paste(&model, {"blue", "red"}, true);
        QTRY_COMPARE(service.count, 1);
        // Even after the minimum spacing, an unread image must not be replaced.
        QTest::qWait(1100);
        QCOMPARE(service.count, 1);
        QCOMPARE(QImage::fromData(service.images[0]), red);
        controller.clipboardRead("image/png");
        QTRY_COMPARE(service.count, 2);
        QCOMPARE(QImage::fromData(service.images[1]), blue);
        QCOMPARE(service.shifts, QList<bool>({false, false}));
        controller.clipboardRead("image/png");
        QTRY_COMPARE(done.count(), 1);
    }
    void imageQueueStopsWhenCancelled() {
        QTemporaryDir directory;
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(directory.filePath("image.png")));
        TestHistory model;
        const QUrl url = QUrl::fromLocalFile(directory.filePath("image.png"));
        model.entries = {{"one", "image", 4, url}, {"two", "image", 4, url}};
        PasteController controller;
        int published = 0;
        controller.publishPayload = [&](const MimePayloads &, QString *) { ++published; return true; };
        controller.paste(&model, {"one", "two"}, false);
        QTRY_COMPARE(service.count, 1);
        controller.clipboardRead("image/png");
        controller.cancel();
        QTest::qWait(1100);
        QCOMPARE(service.count, 1);
        QCOMPARE(published, 1);
    }
};

QTEST_MAIN(PasteControllerTest)
#include "PasteControllerTest.moc"
