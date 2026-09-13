import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Writero

// Hosted AI usage: plan, remaining credits, and the models this account may
// use. Credits are reserved and settled per hosted operation on the server.
Dialog {
    id: usageDialog
    objectName: "writeroUsage"
    title: qsTr("Hosted AI usage")
    modal: true
    focus: true
    width: 420
    anchors.centerIn: Overlay.overlay

    property AccountSession account

    background: Rectangle {
        color: Theme.surface
        border.color: Theme.border
        radius: Theme.radius
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: usageDialog.account && usageDialog.account.connected
                  ? usageDialog.account.accountEmail
                  : qsTr("Not connected")
            color: Theme.text
            font.bold: true
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacing
            rowSpacing: 4

            Label { text: qsTr("Plan") }
            Label {
                text: usageDialog.account ? usageDialog.account.plan : ""
                color: Theme.text
            }

            Label { text: qsTr("Hosted AI") }
            Label {
                text: usageDialog.account && usageDialog.account.hostedAiEnabled
                      ? qsTr("Enabled") : qsTr("Not included")
                color: Theme.text
            }

            Label { text: qsTr("Credits") }
            Label {
                text: usageDialog.account
                      ? qsTr("$%1 remaining").arg(
                            usageDialog.account.remainingCreditUsd.toFixed(2))
                      : ""
                color: Theme.accent
            }
        }

        Label {
            Layout.fillWidth: true
            visible: usageDialog.account && usageDialog.account.hostedAiEnabled
            wrapMode: Text.Wrap
            text: qsTr("Hosted operations reserve credit when they start and settle once "
                       + "when they finish. Personal keys and local models never use credits.")
            color: Theme.textMuted
            font.pixelSize: 12
        }

        Label {
            Layout.fillWidth: true
            visible: usageDialog.account && usageDialog.account.hostedModels.length > 0
            wrapMode: Text.Wrap
            text: qsTr("Models: %1").arg(
                      usageDialog.account ? usageDialog.account.hostedModels.join(", ") : "")
            color: Theme.textFaint
            font.pixelSize: 12
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight

            Button {
                text: qsTr("Refresh")
                onClicked: if (usageDialog.account) usageDialog.account.refreshCapabilities()
            }
            Button {
                text: qsTr("Close")
                onClicked: usageDialog.close()
            }
        }
    }
}
