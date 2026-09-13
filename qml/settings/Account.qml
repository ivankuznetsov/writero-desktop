import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Writero

Dialog {
    id: accountDialog
    objectName: "accountDialog"
    title: qsTr("Writero account")
    modal: true
    focus: true
    width: 460
    anchors.centerIn: Overlay.overlay

    property AccountSession session
    property var usageDialog

    background: Rectangle {
        color: Theme.surface
        border.color: Theme.border
        radius: Theme.radius
    }

    Connections {
        target: accountDialog.session
        function onAuthorizationRequired(url) {
            Qt.openUrlExternally(url)
        }
        function onSignInFinished(success) {
            if (!success && accountDialog.session.lastError !== "")
                statusLabel.text = accountDialog.session.lastError
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing

        Label {
            id: statusLabel
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: accountDialog.session && accountDialog.session.connected
                  ? qsTr("Connected as %1").arg(accountDialog.session.accountEmail)
                  : qsTr("Connect to use cloud documents and hosted AI. "
                         + "Local documents keep working without an account.")
            color: accountDialog.session && accountDialog.session.lastError !== ""
                   ? Theme.danger : Theme.text
        }

        GridLayout {
            Layout.fillWidth: true
            visible: accountDialog.session && accountDialog.session.connected
            columns: 2
            columnSpacing: Theme.spacing
            rowSpacing: 4

            Label { text: qsTr("Plan") }
            Label {
                text: accountDialog.session
                      ? accountDialog.session.plan
                        + (accountDialog.session.subscriptionActive ? "" : qsTr(" (inactive)"))
                      : ""
                color: Theme.text
            }

            Label { text: qsTr("Hosted AI") }
            Label {
                text: accountDialog.session
                      ? (accountDialog.session.hostedAiEnabled
                         ? qsTr("%1 USD in credits")
                               .arg(accountDialog.session.remainingCreditUsd.toFixed(2))
                         : qsTr("Not included in this plan"))
                      : ""
                color: Theme.text
            }

            Label { text: qsTr("Entitlements") }
            Label {
                text: accountDialog.session
                      ? accountDialog.session.entitlements.join(", ")
                      : ""
                color: Theme.textMuted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        Label {
            Layout.fillWidth: true
            visible: accountDialog.session && accountDialog.session.busy
            text: qsTr("Waiting for the browser\u2026")
            color: Theme.textMuted
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight

            Button {
                visible: accountDialog.session && accountDialog.session.connected
                text: qsTr("Usage and credits\u2026")
                onClicked: if (accountDialog.usageDialog) accountDialog.usageDialog.open()
            }

            Button {
                visible: accountDialog.session && accountDialog.session.connected
                text: qsTr("Disconnect")
                onClicked: accountDialog.session.signOut()
            }

            Button {
                visible: accountDialog.session && !accountDialog.session.connected
                text: qsTr("Connect\u2026")
                enabled: accountDialog.session && !accountDialog.session.busy
                onClicked: {
                    statusLabel.text = ""
                    accountDialog.session.signIn()
                }
            }

            Button {
                text: qsTr("Close")
                onClicked: accountDialog.close()
            }
        }
    }
}
