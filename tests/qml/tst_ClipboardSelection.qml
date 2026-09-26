import QtQuick
import QtTest
import "../../plasmoid/contents/ui" as Cliprove

Item {
    width: 640
    height: 300
    QtObject {
        id: menu
        property var selectedUuids: []
        property int clicks: 0
        property int batches: 0
        property int starMetricsImplicitWidth: 24
        property int starMetricsImplicitHeight: 24
        signal expandedChanged()
        function isMultiSelected(uuid) { return selectedUuids.indexOf(uuid) >= 0; }
        function toggleMultiSelection(uuid, row) {
            const next = selectedUuids.slice();
            const index = next.indexOf(uuid);
            if (index >= 0) next.splice(index, 1); else next.push(uuid);
            selectedUuids = next;
        }
        function selectedUuidList() { return selectedUuids.slice(); }
        function itemSelected(uuid) { clicks++; }
        function pasteSelection(uuids) { batches++; }
    }
    ListView {
        id: list
        anchors.fill: parent
        property var clipboardMenu: menu
        model: ListModel {
            ListElement { uuid: "one"; display: "First entry"; type: 2 }
            ListElement { uuid: "two"; display: "Second entry"; type: 2 }
        }
        delegate: Cliprove.TextItemDelegate {
            decoration: ""
            imageSize: Qt.size(0, 0)
            listMargins: ({left: 0, right: 0, top: 0, bottom: 0})
        }
    }
    TestCase {
        name: "ClipboardSelection"
        when: windowShown
        function init() {
            menu.selectedUuids = [];
            menu.clicks = menu.batches = 0;
            list.currentIndex = 0;
        }
        function test_controlClickTextAndBackground() {
            const first = list.itemAtIndex(0);
            const second = list.itemAtIndex(1);
            mouseClick(first, 35, first.height / 2, Qt.LeftButton, Qt.ControlModifier);
            compare(menu.clicks, 0);
            compare(menu.selectedUuids.length, 1);
            verify(first.multiSelected);
            mouseClick(second, 8, second.height / 2, Qt.LeftButton, Qt.ControlModifier);
            compare(menu.clicks, 0);
            compare(menu.selectedUuids.length, 2);
            mouseClick(first, 35, first.height / 2, Qt.LeftButton, Qt.ControlModifier);
            compare(menu.selectedUuids.length, 1);
            compare(menu.clicks, 0);
        }
        function test_plainClickTextOnlyOnce() {
            const first = list.itemAtIndex(0);
            mouseClick(first, 35, first.height / 2);
            compare(menu.clicks, 1);
            compare(menu.selectedUuids.length, 0);
        }
        function test_enterSubmitsBatch() {
            const first = list.itemAtIndex(0);
            mouseClick(first, 35, first.height / 2, Qt.LeftButton, Qt.ControlModifier);
            first.forceActiveFocus();
            keyClick(Qt.Key_Return);
            compare(menu.batches, 1);
            compare(menu.clicks, 0);
        }
    }
}
