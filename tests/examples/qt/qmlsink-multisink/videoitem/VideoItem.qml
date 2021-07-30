import QtQuick 2.0
import ACME.VideoItem 1.0
import org.freedesktop.gstreamer.GLVideoItem 1.0

VideoItem {
    id: videoitem

    GstGLVideoItem {
        id: video
        objectName: "videoItem"
        anchors.fill: parent
    }
}
