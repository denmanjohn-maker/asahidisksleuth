import QtQuick
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    title: "Disk Sleuth"
    width: 1100
    height: 750
    minimumWidth: 700
    minimumHeight: 480

    pageStack.initialPage: welcomePage
    pageStack.defaultColumnWidth: Kirigami.Units.gridUnit * 30

    Component { id: welcomePage; WelcomePage {} }
    Component { id: resultsPage; ResultsPage {} }

    Connections {
        target: controller
        function onStateChanged() {
            if (controller.state === 1) { // Scanning — stay, progress shown on welcome page
            } else if (controller.state === 2) { // Results
                root.pageStack.replace(resultsPage);
            } else {
                root.pageStack.replace(welcomePage);
            }
        }
        function onErrorOccurred(message) {
            errorDialog.text = message;
            errorDialog.open();
        }
    }

    Kirigami.PromptDialog {
        id: errorDialog
        property alias text: errorLabel.text
        title: "Scan failed"
        standardButtons: Kirigami.Dialog.Ok
        Controls.Label { id: errorLabel; wrapMode: Text.WordWrap }
    }
}
