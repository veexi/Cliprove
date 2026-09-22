/*
    Cliprove popup, based on KDE Plasma KlipperPopup.qml.
    SPDX-License-Identifier: GPL-2.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Window
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.plasma5support as Plasma5Support
import org.kde.kirigami as Kirigami
import org.kde.plasma.private.clipboard as Private

PlasmaComponents.Page {
    id: dialogItem

    implicitWidth: 100
    implicitHeight: 100

    signal requestHidePopup()

    focus: true
    header: stack.currentItem.header as Item

    property var batchPasteUuids: []
    property int batchPasteIndex: 0
    Keys.onEscapePressed: requestHidePopup()
    Keys.forwardTo: [stack.currentItem]

    function updateContentSize(screenSize: size): void {
        dialogItem.implicitWidth = Math.max(Kirigami.Units.gridUnit * 15,
                                            Math.min(screenSize.width / 4, Kirigami.Units.gridUnit * 40));
        dialogItem.implicitHeight = Math.min(screenSize.height / 2,
                                             Kirigami.Units.gridUnit * 40);
    }

    function startBatchPaste(uuids): void {
        if (!uuids || uuids.length === 0)
            return;
        batchPasteUuids = uuids.slice();
        batchPasteIndex = 0;
        requestHidePopup();
        batchNextTimer.restart();
    }

    Plasma5Support.DataSource {
        id: pasteCommand
        engine: "executable"

        function triggerPaste(): void {
            const command = "qdbus org.veexi.CliprovePaste /Paste org.veexi.CliprovePaste.paste # " + Date.now();
            connectSource(command);
        }

        onNewData: (sourceName, data) => disconnectSource(sourceName)
    }
    Timer {
        id: singlePasteTimer
        interval: 250
        repeat: false
        onTriggered: pasteCommand.triggerPaste()
    }

    Timer {
        id: batchNextTimer
        interval: 140
        repeat: false
        onTriggered: {
            if (dialogItem.batchPasteIndex >= dialogItem.batchPasteUuids.length) {
                dialogItem.batchPasteUuids = [];
                dialogItem.batchPasteIndex = 0;
                return;
            }
            historyModel.moveToTop(dialogItem.batchPasteUuids[dialogItem.batchPasteIndex]);
            batchPasteDelayTimer.restart();
        }
    }

    Timer {
        id: batchPasteDelayTimer
        interval: 90
        repeat: false
        onTriggered: {
            pasteCommand.triggerPaste();
            dialogItem.batchPasteIndex++;
            if (dialogItem.batchPasteIndex < dialogItem.batchPasteUuids.length)
                batchNextTimer.restart();
            else {
                dialogItem.batchPasteUuids = [];
                dialogItem.batchPasteIndex = 0;
            }
        }
    }
    Private.HistoryModel {
        id: historyModel
    }

    Connections {
        target: dialogItem.Window.window

        function onVisibleChanged() {
            if (dialogItem.Window.window.visible) {
                clipboardMenu.clearFilter();
                (clipboardMenu.view as ListView).currentIndex = 0;
                (clipboardMenu.view as ListView).positionViewAtBeginning();
            }
        }
    }

    Item {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.max(8, Kirigami.Units.smallSpacing * 1.5)
        z: 1000

        HoverHandler {
            cursorShape: Qt.SizeAllCursor
        }

        DragHandler {
            target: null
            acceptedButtons: Qt.LeftButton
            onActiveChanged: if (active) {
                dialogItem.Window.window.startSystemMove();
            }
        }
    }

    QQC.StackView {
        id: stack
        anchors.fill: parent

        initialItem: ClipboardMenu {
            id: clipboardMenu
            expanded: dialogItem.Window.window ? dialogItem.Window.window.visible : false
            dialogItem: dialogItem
            model: historyModel
            showsClearHistoryButton: true
            barcodeType: "QRCode"

            onItemSelected: {
                dialogItem.requestHidePopup();
                singlePasteTimer.restart();
            }
            onPasteSelection: uuids => dialogItem.startBatchPaste(uuids)
        }
    }
}
