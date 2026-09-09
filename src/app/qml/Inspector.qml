import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

// Truth card for the selected node + actions (reveal, trash).
Item {
    id: root
    property int node: -1

    visible: node >= 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        Controls.Label {
            Layout.fillWidth: true
            text: node >= 0 ? controller.nodePath(node) : ""
            elide: Text.ElideMiddle
            font.bold: true
        }

        Controls.Label {
            Layout.fillWidth: true
            opacity: 0.7
            visible: text !== ""
            text: node >= 0 ? controller.nodeBadges(node) : ""
        }

        GridLayout {
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: 2
            visible: node >= 0

            Controls.Label { opacity: 0.6; text: "Logical" }
            Controls.Label { text: node >= 0 ? controller.formatBytes(controller.nodeLogical(node)) : "" }
            Controls.Label { opacity: 0.6; text: "Physical" }
            Controls.Label { text: node >= 0 ? controller.formatBytes(controller.nodePhysical(node)) : "" }
            Controls.Label { opacity: 0.6; text: "Freeable now" }
            Controls.Label {
                font.bold: true
                color: Kirigami.Theme.positiveTextColor
                text: node >= 0 ? controller.formatBytes(controller.nodeUnique(node)) : ""
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Kirigami.Units.smallSpacing

            Controls.Button {
                icon.name: "folder-open"
                text: "Open"
                enabled: root.node >= 0
                onClicked: controller.revealInFileManager(root.node)
            }
            Controls.Button {
                icon.name: "user-trash"
                text: "Move to Trash"
                enabled: root.node > 0
                onClicked: trashDialog.open()
            }
        }
    }

    Kirigami.PromptDialog {
        id: trashDialog
        title: "Move to Trash?"
        subtitle: root.node >= 0
            ? "\"" + controller.nodeName(root.node) + "\" — frees ~"
              + controller.formatBytes(controller.nodeUnique(root.node))
            : ""
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: controller.moveToTrash(root.node)
    }
}
