/*
    Cliprove popup, based on KDE Plasma KlipperPopup.qml.
    SPDX-License-Identifier: GPL-2.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC
import QtQuick.Window
import org.kde.plasma.components as PC
import org.kde.plasma.extras as PlasmaExtras
import org.kde.kirigami as Kirigami
import org.kde.plasma.private.clipboard as Private

PlasmaExtras.Representation {
    id: dialogItem

    implicitWidth: 100
    implicitHeight: 100

    signal requestHidePopup()

    focus: true
    collapseMarginsHint: true
    header: stack.currentItem.header as Item

    property bool terminalPaste: false
    Keys.onEscapePressed: requestHidePopup()
    Keys.forwardTo: [stack.currentItem]

    function updateContentSize(screenSize: size): void {
        dialogItem.implicitWidth = Math.max(Kirigami.Units.gridUnit * 15,
                                            Math.min(screenSize.width / 4, Kirigami.Units.gridUnit * 40));
        dialogItem.implicitHeight = Math.min(screenSize.height / 2,
                                             Kirigami.Units.gridUnit * 40);
    }

    function startPaste(uuids): void {
        pasteController.paste(historyModel, uuids, terminalPaste);
    }

    footer: PC.Label {
        text: pasteController.error || (pasteController.busy ? "正在粘贴…"
            : clipboardMenu.selectedUuids.length > 0
              ? "已选择 " + clipboardMenu.selectedUuids.length + " 条 · Enter 粘贴所选"
              : "点击即粘贴 · Ctrl + 点击多选 · Enter 粘贴所选")
        wrapMode: Text.Wrap
        padding: Kirigami.Units.smallSpacing
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

            enabled: !pasteController.busy
            onItemSelected: uuid => dialogItem.startPaste([uuid])
            onPasteSelection: uuids => dialogItem.startPaste(uuids)
        }
    }
}
