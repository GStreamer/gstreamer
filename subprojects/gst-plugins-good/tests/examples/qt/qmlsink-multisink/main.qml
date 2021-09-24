import QtQuick 2.4
import QtQuick.Controls 1.1

import "videoitem"

ApplicationWindow {
    visible: true

    minimumWidth: videowall.cellWidth * Math.sqrt(videowall.model.length) + videowall.leftMargin
    minimumHeight: videowall.cellHeight * Math.sqrt(videowall.model.length) + 32
    maximumWidth: minimumWidth
    maximumHeight: minimumHeight

    GridView {
        id: videowall
        leftMargin: 10
        model: patterns
        anchors.fill: parent
        cellWidth: 500
        cellHeight: 500
        delegate: Rectangle {
            border.color: "darkgray"
            width: videowall.cellWidth - 10
            height: videowall.cellHeight - 10
            radius: 3
            Label {
                anchors.centerIn: parent
                text: "No signal"
            }
            Loader {
                active: playing.checked
                anchors.fill: parent
                anchors.margins: 1
                sourceComponent: VideoItem {
                    id: player
                    source: playing.checked ? modelData : ""
                }
            }
            Row {
                anchors.margins: 1
                id: controls
                height: 32
                spacing: 10
                Button {
                    id: playing
                    checkable: true
                    checked: true
                    width: height
                    height: controls.height
                    text: index
                }
                Label {
                    verticalAlignment: Qt.AlignVCenter
                    height: controls.height
                    text: modelData
                }
            }
        }
    }
}
