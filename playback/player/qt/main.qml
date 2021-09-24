/* GStreamer
 *
 * Copyright (C) 2015 Alexandre Moreno <alexmorenocano@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

import QtQuick 2.4
import QtQuick.Controls 1.1
import QtQuick.Controls.Styles 1.3
import QtQuick.Dialogs 1.2
import QtQuick.Window 2.1
import extension 1.0
import org.freedesktop.gstreamer.GLVideoItem 1.0

import "fontawesome.js" as FontAwesome

ApplicationWindow {
    id: window
    visible: true
    width: 640
    height: 480
    x: 30
    y: 30
    color: "black"
//    title : player.mediaInfo.title

    Player {
        id: player
        objectName: "player"
        volume: 0.5
        autoPlay: false

        onStateChanged: {
            if (state === Player.STOPPED) {
                playbutton.state = "play"
            }
        }

        onResolutionChanged: {
            if (player.videoAvailable) {
                window.width = resolution.width
                window.height = resolution.height
            }
        }
    }

    GstGLVideoItem {
        id: video
        objectName: "videoItem"
        anchors.centerIn: parent
        width: parent.width
        height: parent.height
        visible: player.videoAvailable
    }

    ImageSample {
        id: sample
        anchors.centerIn: parent
        sample: player.mediaInfo.sample
        width: parent.width
        height: parent.height
        visible: !player.videoAvailable
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onPositionChanged: {
            playbar.opacity = 1.0
            hidetimer.start()
        }
    }

    Timer {
        id: hidetimer
        interval: 5000
        onTriggered: {
            if (!playbarMouseArea.containsMouse) {
                playbar.opacity = 0.0
                settings.visible = false
            }
            stop()
        }
    }

    FileDialog {
        id: fileDialog
        //nameFilters: [TODO globs from mime types]
        onAccepted: player.source = fileUrl
    }

    Action {
        id: fileOpenAction
        text: "Open"
        onTriggered: fileDialog.open()
    }

    Item {
        anchors.fill: parent
        FontLoader {
            source: "fonts/fontawesome-webfont.ttf"
        }



        Rectangle {
            id : playbar
            color: Qt.rgba(1, 1, 1, 0.7)
            border.width: 1
            border.color: "white"
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 15
            anchors.horizontalCenter: parent.horizontalCenter
            width : grid.width + 20
            height: 40//childrenRect.height + 20
            radius: 5
            focus: true

            MouseArea {
                id: playbarMouseArea
                anchors.fill: parent
                hoverEnabled: true
            }

            Rectangle {
                id: settings
                width: 150; height: settingsView.contentHeight
                color: Qt.rgba(1, 1, 1, 0.7)
                anchors.right: parent.right
                anchors.bottom: parent.top
                anchors.bottomMargin: 3
                border.width: 1
                border.color: "white"
                radius: 5
                visible: false

                ListModel {
                    id: settingsModel
                    ListElement {
                        name: "Video"
                    }
                    ListElement {
                        name: "Audio"
                    }
                    ListElement {
                        name: "Subtitle"
                    }
                }

                Component {
                    id: settingsDelegate
                    Item {
                        width: 150; height: 20
                        Text {
                            text: model.name
                            font.pixelSize: 13
                            anchors.centerIn: parent
                        }

                        Text {
                            font.pixelSize: 13
                            font.family: "FontAwesome"
                            text: FontAwesome.Icon.ChevronRight
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        MouseArea {
                           anchors.fill: parent
                           onClicked: {
                               switch(name) {
                               case 'Video':
                                   videos.visible = true
                                   break
                               case 'Audio':
                                   audios.visible = true
                                   break
                               case 'Subtitle' :
                                   subtitles.visible = true
                                   break
                               }
                               settings.visible = false
                           }
                        }
                    }
                }

                ListView {
                    id: settingsView
                    anchors.fill: parent
                    model: settingsModel
                    delegate: settingsDelegate
                }
            }

            Rectangle {
                id: videos
                width: 150; height: videoView.contentHeight
                color: Qt.rgba(1, 1, 1, 0.7)
                anchors.right: parent.right
                anchors.bottom: parent.top
                anchors.bottomMargin: 3
                border.width: 1
                border.color: "white"
                radius: 5

                property bool selected: ListView.isCurrentItem
                visible: false

                Component {
                    id: videoDelegate
                    Item {
                        width: 150; height: 20
                        Text {
                            text: model.modelData.resolution.width + 'x' + model.modelData.resolution.height
                            font.pixelSize: 13
                            anchors.centerIn: parent
                        }

                        MouseArea {
                           anchors.fill: parent
                           onClicked: {
                               parent.ListView.view.currentIndex = index
                               player.currentVideo = model.modelData
                           }
                       }
                    }
                }

                ListView {
                    id : videoView
                    anchors.fill: parent
                    model: player.mediaInfo.videoStreams
                    delegate: videoDelegate
                    highlight: Rectangle {
                        color: "white"
                        radius: 5
                        border.width: 1
                        border.color: "white"
                    }
                    focus: true
                    clip: true
                }
            }

            Rectangle {
                id: audios
                width: 150; height: audioView.contentHeight
                color: Qt.rgba(1, 1, 1, 0.7)
                anchors.right: parent.right
                anchors.bottom: parent.top
                anchors.bottomMargin: 3
                border.width: 1
                border.color: "white"
                radius: 5

                property bool selected: ListView.isCurrentItem
                visible: false

                Component {
                    id: audioDelegate
                    Item {
                        width: 150; height: 20
                        Text {
                            text: model.modelData.channels + 'channels'
                            font.pixelSize: 13
                            anchors.centerIn: parent
                        }

                        MouseArea {
                           anchors.fill: parent
                           onClicked: {
                               parent.ListView.view.currentIndex = index
                               player.currentAudio = model.modelData
                           }
                       }
                    }
                }

                ListView {
                    id : audioView
                    anchors.fill: parent
                    model: player.mediaInfo.audioStreams
                    delegate: audioDelegate
                    highlight: Rectangle {
                        color: "white"
                        radius: 5
                        border.width: 1
                        border.color: "white"
                    }
                    focus: true
                    clip: true
                }
            }

            Rectangle {
                id: subtitles
                width: 150; height: subtitleView.contentHeight
                color: Qt.rgba(1, 1, 1, 0.7)
                anchors.right: parent.right
                anchors.bottom: parent.top
                anchors.bottomMargin: 3
                border.width: 1
                border.color: "white"
                radius: 5

                property bool selected: ListView.isCurrentItem
                visible: false

                Component {
                    id: subtitleDelegate
                    Item {
                        width: 150; height: 20
                        Text {
                            text: model.modelData.language
                            font.pixelSize: 13
                            anchors.centerIn: parent
                        }

                        MouseArea {
                           anchors.fill: parent
                           onClicked: {
                               parent.ListView.view.currentIndex = index
                               player.currentSubtitle = model.modelData
                           }
                       }
                    }
                }

                ListView {
                    id : subtitleView
                    anchors.fill: parent
                    model: player.mediaInfo.subtitleStreams
                    delegate: subtitleDelegate
                    highlight: Rectangle {
                        color: "white"
                        radius: 5
                        border.width: 1
                        border.color: "white"
                    }
                    focus: true
                    clip: true
                }
            }

            Grid {
                id: grid
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 7
                rows: 1
                verticalItemAlignment: Qt.AlignVCenter

                Text {
                    id : openmedia
                    font.pixelSize: 17
                    font.family: "FontAwesome"
                    text: FontAwesome.Icon.FolderOpen

                    MouseArea {
                       anchors.fill: parent
                       onPressed: fileDialog.open()
                    }
                }

                Item {
                    width: 17
                    height: 17

                    Text {
                        anchors.centerIn: parent
                        font.pixelSize: 17
                        font.family: "FontAwesome"
                        text: FontAwesome.Icon.StepBackward
                    }

                    MouseArea {
                       anchors.fill: parent
                       onPressed: player.previous()
                    }
                }

                Item {
                    width: 25
                    height: 25

                    Text {
                        anchors.centerIn: parent
                        id : playbutton
                        font.family: "FontAwesome"
                        state: "play"

                        states: [
                            State {
                                name: "play"
                                PropertyChanges {
                                    target: playbutton
                                    text: FontAwesome.Icon.PlayCircle
                                    font.pixelSize: 25
                                }
                            },
                            State {
                                name: "pause"
                                PropertyChanges {
                                    target: playbutton
                                    text: FontAwesome.Icon.Pause
                                    font.pixelSize: 17
                                }
                            }
                        ]
                    }

                    MouseArea {
                       id: playArea
                       anchors.fill: parent
                       onPressed: {
                           if (player.state !== Player.PLAYING) {
                               player.play()
                               playbutton.state = "pause"
                           } else {
                               player.pause()
                               playbutton.state = "play"
                           }
                       }
                    }
                }

                Item {
                    width: 17
                    height: 17

                    Text {
                        anchors.centerIn: parent
                        font.pixelSize: 17
                        font.family: "FontAwesome"
                        text: FontAwesome.Icon.StepForward
                    }

                    MouseArea {
                       anchors.fill: parent
                       onPressed: player.next()
                    }
                }

                Item {
                    width: 40
                    height: 17
                    Text {
                        id: timelabel
                        anchors.centerIn: parent
                        font.pixelSize: 13
                        color: "black"
                        text: {
                            var current = new Date(Math.floor(slider.value / 1e6));
                            current.getMinutes() + ":" + ('0'+current.getSeconds()).slice(-2)
                        }
                    }
                }

                Item {
                    width: 200
                    height: 38
                    Text {
                        anchors.centerIn: parent
                        width: parent.width
                        text: player.mediaInfo.title
                        font.pixelSize: 15
                        elide: Text.ElideRight
                    }
                }

                Item {
                    width: 40
                    height: 17
                    Text {
                        id: durationlabel
                        anchors.centerIn: parent
                        font.pixelSize: 13
                        color: "black"
                        text: {
                            var duration = new Date(Math.floor(player.duration / 1e6));
                            duration.getMinutes() + ":" + ('0'+duration.getSeconds()).slice(-2)
                        }
                    }
                }

                Text {
                    id: sub
                    font.pixelSize: 17
                    font.family: "FontAwesome"
                    text: FontAwesome.Icon.ClosedCaptions
                    color: player.subtitleEnabled ? "red" : "black"

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            player.subtitleEnabled = !player.subtitleEnabled
                        }
                    }
                }

                Item {
                    width: 17
                    height: 17

                    Text {
                        id : volume
                        anchors.centerIn: parent
                        font.pixelSize: 17
                        font.family: "FontAwesome"
                        text: {
                            if (volumeslider.value > volumeslider.maximumValue / 2) {
                                FontAwesome.Icon.VolumeUp
                            } else if (volumeslider.value === 0) {
                                FontAwesome.Icon.VolumeOff
                            } else {
                                FontAwesome.Icon.VolumeDown
                            }
                        }
                    }

                    Rectangle {
                        id : volumebar
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.top
                        //anchors.bottomMargin:3
                        color: "lightgray"
                        width: 17
                        height: 66
                        visible: false
                        radius: 5

                        Slider {
                            id: volumeslider
                            value: player.volume
                            minimumValue: 0.0
                            maximumValue: 1.0
                            stepSize: 0.001
                            anchors.centerIn: parent
                            orientation: Qt.Vertical
                            onPressedChanged: player.volume = value

                            style: SliderStyle {
                                groove: Item {
                                    implicitWidth: 47
                                    implicitHeight: 3
                                    anchors.centerIn: parent                                    

                                    Rectangle {
                                        antialiasing: true
                                        height: parent.height
                                        width: parent.width
                                        color: "gray"
                                        opacity: 0.8
                                        radius: 5

                                        Rectangle {
                                            antialiasing: true
                                            height: parent.height
                                            width: parent.width * control.value / control.maximumValue
                                            color: "red"
                                            radius: 5
                                        }
                                    }
                                }
                                handle: Rectangle {
                                    anchors.centerIn: parent
                                    color: control.pressed ? "white" : "lightgray"
                                    border.color: "gray"
                                    border.width: 1
                                    implicitWidth: 11
                                    implicitHeight: 11
                                    radius: 90
                                }
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onPressed: {
                            volumebar.visible = !volumebar.visible
                        }
                    }

                    MouseArea {
                        anchors.fill: volumebar
                        hoverEnabled: true
                        propagateComposedEvents: true

                        onClicked: mouse.accepted = false;
                        onPressed: mouse.accepted = false;
                        onReleased: mouse.accepted = false;
                        onDoubleClicked: mouse.accepted = false;
                        onPositionChanged: mouse.accepted = false;
                        onPressAndHold: mouse.accepted = false;

                        onExited: {
                            volumebar.visible = false
                        }
                    }

                }

                Text {
                    id: cog
                    font.pixelSize: 17
                    font.family: "FontAwesome"
                    text: FontAwesome.Icon.Cog

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            settings.visible = !settings.visible
                            videos.visible = false
                            audios.visible = false
                            subtitles.visible = false


                        }
                    }
                }

                Text {
                    id : fullscreen
                    font.pixelSize: 17
                    font.family: "FontAwesome"
                    text: FontAwesome.Icon.ResizeFull

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (window.visibility === Window.FullScreen) {
                                window.showNormal()
                                fullscreen.text = FontAwesome.Icon.ResizeFull
                            } else {
                                window.showFullScreen()
                                fullscreen.text = FontAwesome.Icon.ResizeSmall
                            }
                        }
                    }
                }
            }

            Item {
                width: playbar.width
                height: 5
                anchors.bottom: playbar.bottom

                Slider {
                    id: slider
                    maximumValue: player.duration
                    value: player.position
                    onPressedChanged: player.seek(value)
                    onValueChanged: {
                        if (pressed)
                            player.seek(value)
                    }
                    enabled: player.mediaInfo.seekable
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    updateValueWhileDragging: true

                    MouseArea {
                        id: sliderMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        propagateComposedEvents: true

                        onClicked: mouse.accepted = false;
                        onPressed: mouse.accepted = false;
                        onReleased: mouse.accepted = false;
                        onDoubleClicked: mouse.accepted = false;
                        onPositionChanged: mouse.accepted = false;
                        onPressAndHold: mouse.accepted = false;
                    }

                    style: SliderStyle {
                        groove: Item {
                            implicitWidth: playbar.width
                            implicitHeight: 5

                            Rectangle {
                                height: parent.height
                                width: parent.width
                                anchors.verticalCenter: parent.verticalCenter
                                color: "gray"
                                opacity: 0.8

                                Rectangle {
                                    antialiasing: true
                                    color: "red"
                                    height: parent.height
                                    width: parent.width * control.value / control.maximumValue
                                }

                                Rectangle {
                                    antialiasing: true
                                    color: "yellow"
                                    height: parent.height
                                    width: parent.width * player.buffering / 100
                                }
                            }
                        }
                        handle: Rectangle {
                            anchors.centerIn: parent
                            color: control.pressed ? "white" : "lightgray"
                            implicitWidth: 4
                            implicitHeight: 5
                        }
                    }
                }
            }
        }
    }
}
