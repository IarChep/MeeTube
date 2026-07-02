import QtQuick 1.1
import com.nokia.meego 1.0
import "../../js/UIConstants.js" as UI

// OAuth TV device-code sign-in (innertube.auth()): requests a code, shows it plus
// a QR of the verification URL, then waits while the manager polls for approval.
// Auto-closes and opens AccountPage once authenticated.
Sheet {
    id: sheet

    // "request" (fetching a code) | "wait" (user enters it elsewhere) | "error"
    property string phase: "request"
    property string userCode: ""
    property string verificationUrl: ""
    property string errorText: ""

    rejectButtonText: "Cancel"
    onRejected: innertube.auth().cancel()

    onStatusChanged: {
        if (status === DialogStatus.Opening)
            sheet.begin();
    }

    function begin() {
        sheet.phase = "request";
        sheet.userCode = "";
        sheet.verificationUrl = "";
        sheet.errorText = "";
        innertube.auth().signIn();
    }

    Connections {
        target: innertube.auth()
        onUserCodeReady: {
            sheet.verificationUrl = verificationUrl;
            sheet.userCode = userCode;
            sheet.phase = "wait";
        }
        onAuthenticated: {
            sheet.close();
            pageStack.push(Qt.resolvedUrl("../../pages/AccountPage.qml"));
        }
        onAuthFailed: {
            sheet.errorText = error;
            sheet.phase = "error";
        }
    }

    content: Item {
        anchors.fill: parent

        // --- phase: request (fetching the device code)
        Column {
            visible: sheet.phase === "request"
            anchors.centerIn: parent
            spacing: UI.PADDING_XLARGE

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: sheet.phase === "request"
                platformStyle: BusyIndicatorStyle { size: "large" }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Requesting code…"
                color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                font.pixelSize: UI.FONT_LARGE
                font.family: UI.FONT_FAMILY
            }
        }

        // --- phase: wait (code + QR, manager polls in C++)
        Column {
            visible: sheet.phase === "wait"
            anchors {
                top: parent.top; topMargin: UI.PADDING_XXLARGE
                left: parent.left
                right: parent.right
            }
            spacing: UI.PADDING_XLARGE

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Sign in with YouTube"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
                font.bold: true
            }
            Text {
                width: parent.width - UI.DEFAULT_MARGIN * 2
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                text: "On your phone or computer, open youtube.com/activate and enter this code:"
                color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                font.pixelSize: UI.FONT_SMALL
                font.family: UI.FONT_FAMILY
                wrapMode: Text.WordWrap
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: sheet.userCode
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE * 2
                font.family: UI.FONT_FAMILY
                font.bold: true
                font.letterSpacing: 4
            }
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 220
                height: 220
                sourceSize.width: 220
                source: sheet.verificationUrl !== ""
                        ? "image://qr/" + encodeURIComponent(sheet.verificationUrl)
                        : ""
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: UI.PADDING_LARGE

                BusyIndicator {
                    anchors.verticalCenter: parent.verticalCenter
                    running: sheet.phase === "wait"
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Waiting for confirmation…"
                    color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                    font.pixelSize: UI.FONT_SMALL
                    font.family: UI.FONT_FAMILY
                }
            }
        }

        // --- phase: error
        Column {
            visible: sheet.phase === "error"
            anchors.centerIn: parent
            spacing: UI.PADDING_XLARGE

            Text {
                width: sheet.width - UI.DEFAULT_MARGIN * 2
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                text: "Sign-in failed"
                color: UI.COLOR_INVERTED_FOREGROUND
                font.pixelSize: UI.FONT_XLARGE
                font.family: UI.FONT_FAMILY
                font.bold: true
            }
            Text {
                width: sheet.width - UI.DEFAULT_MARGIN * 2
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                text: sheet.errorText
                color: UI.COLOR_INVERTED_SECONDARY_FOREGROUND
                font.pixelSize: UI.FONT_SMALL
                font.family: UI.FONT_FAMILY
                wrapMode: Text.WordWrap
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Retry"
                onClicked: sheet.begin()
            }
        }
    }
}
