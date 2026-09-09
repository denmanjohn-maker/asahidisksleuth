import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: page
    padding: 0

    header: ColumnLayout {
        spacing: 0

        // Toolbar: breadcrumbs + lens toggle + actions
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            Controls.Button {
                icon.name: "go-previous"
                text: "Up"
                enabled: controller.focusNode !== 0
                onClicked: {
                    const parent = controller.nodeParent(controller.focusNode);
                    if (parent >= 0) controller.focusNode = parent;
                }
            }

            // Breadcrumbs
            Repeater {
                model: controller.breadcrumbs
                Controls.Button {
                    flat: true
                    text: modelData
                    onClicked: {
                        // Walk from root down to this crumb.
                        let node = 0;
                        for (let i = 1; i <= index; ++i) {
                            const kids = controller.nodeChildren(node);
                            for (const k of kids) {
                                if (controller.nodeName(k) === controller.breadcrumbs[i]) {
                                    node = k; break;
                                }
                            }
                        }
                        controller.focusNode = node;
                    }
                }
            }

            Item { Layout.fillWidth: true }

            Controls.Button {
                icon.name: "view-refresh"
                text: "Rescan"
                onClicked: controller.scan(controller.rootPath)
            }

            Controls.ButtonGroup { id: lensGroup }
            Controls.RadioButton {
                text: "Physical"
                checked: controller.lens === 1
                Controls.ButtonGroup.group: lensGroup
                onClicked: controller.lens = 1
            }
            Controls.RadioButton {
                text: "Logical"
                checked: controller.lens === 0
                Controls.ButtonGroup.group: lensGroup
                onClicked: controller.lens = 0
            }
            Controls.RadioButton {
                text: "Freeable"
                checked: controller.lens === 2
                Controls.ButtonGroup.group: lensGroup
                onClicked: controller.lens = 2
            }
        }

        // Summary line
        Controls.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            opacity: 0.7
            text: controller.totalFiles().toLocaleString() + " files · "
                + controller.totalDirs().toLocaleString() + " folders · "
                + controller.wallSeconds().toFixed(1) + "s"
                + (controller.deniedCount() > 0
                   ? "  ·  ⚠ " + controller.deniedCount() + " unreadable" : "")
        }
    }

    // Main content: sunburst | tree | inspector
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Left: sunburst
        Item {
            Layout.preferredWidth: parent.width * 0.42
            Layout.fillHeight: true
            Layout.margins: Kirigami.Units.smallSpacing

            Sunburst {
                id: sunburst
                anchors.fill: parent
                onSegmentClicked: (node, isDir, isOther) => {
                    if (isOther) return;
                    if (isDir) controller.focusNode = node;
                    controller.selectedNode = node;
                }
                onSegmentHovered: (node, name, sizeText, isOther) => {
                    hoverLabel.text = node >= 0 || isOther ? name + "  —  " + sizeText : "";
                }
            }

            Controls.Label {
                id: hoverLabel
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                opacity: 0.75
                elide: Text.ElideMiddle
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Kirigami.Separator { Layout.fillHeight: true }

        // Right column: tree (top) + inspector (bottom)
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Tree of the focus node's children
            ListView {
                id: treeList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: controller.nodeChildren(controller.focusNode)

                delegate: Controls.ItemDelegate {
                    id: delegateItem
                    width: treeList.width
                    required property int modelData
                    highlighted: controller.selectedNode === modelData
                    onClicked: controller.selectedNode = modelData
                    onDoubleClicked: {
                        if (controller.nodeIsDir(modelData)) controller.focusNode = modelData;
                    }

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Controls.Label {
                            text: controller.nodeIsDir(delegateItem.modelData) ? "📁" : "📄"
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            text: controller.nodeName(delegateItem.modelData)
                            elide: Text.ElideMiddle
                        }
                        Controls.Label {
                            opacity: 0.6
                            text: controller.nodeBadges(delegateItem.modelData)
                            visible: text !== ""
                        }
                        Controls.Label {
                            font.bold: true
                            text: controller.formatBytes(controller.nodeSize(delegateItem.modelData))
                        }
                    }
                }
            }

            Kirigami.Separator { Layout.fillWidth: true }

            // Inspector for the selection
            Inspector {
                Layout.fillWidth: true
                Layout.preferredHeight: Kirigami.Units.gridUnit * 9
                node: controller.selectedNode
            }
        }
    }
}
