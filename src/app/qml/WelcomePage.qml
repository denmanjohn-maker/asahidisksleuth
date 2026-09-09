import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: page
    title: "Disk Sleuth"
    padding: 0

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.7, Kirigami.Units.gridUnit * 34)
        spacing: Kirigami.Units.largeSpacing * 2

        Kirigami.Heading {
            Layout.alignment: Qt.AlignHCenter
            text: "Where did the space go?"
            level: 1
        }

        Controls.Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: "Scan a folder or the whole disk. Sizes are honest: "
                + "logical is what files claim, physical is what the disk holds."
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Kirigami.Units.largeSpacing

            Controls.Button {
                icon.name: "drive-harddisk"
                text: "Scan Home"
                highlighted: true
                onClicked: controller.scan("/home")
            }
            Controls.Button {
                icon.name: "folder-open"
                text: "Scan Folder…"
                onClicked: controller.pickAndScan()
            }
            Controls.Button {
                icon.name: "computer"
                text: "Scan /"
                onClicked: controller.scan("/")
            }
        }

        // Live progress while scanning
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing
            visible: controller.state === 1

            Controls.ProgressBar { Layout.fillWidth: true; indeterminate: true }

            Controls.Label {
                Layout.alignment: Qt.AlignHCenter
                text: controller.filesSeen.toLocaleString() + " files · "
                    + controller.formatBytes(controller.physicalBytes)
                font.bold: true
            }
            Controls.Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
                opacity: 0.6
                text: controller.currentPath
            }
            Controls.Button {
                Layout.alignment: Qt.AlignHCenter
                text: "Cancel"
                onClicked: controller.cancelScan()
            }
        }
    }
}
