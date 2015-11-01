TEMPLATE = app

QT += qml quick widgets

CONFIG += c++11

DEFINES += GST_USE_UNSTABLE_API

INCLUDEPATH += ../lib

RESOURCES += qml.qrc

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Default rules for deployment.
include(deployment.pri)

# not tested (yet)
unix:!macx {
QT_CONFIG -= no-pkg-config
CONFIG += link_pkgconfig
PKGCONFIG = \
    gstreamer-1.0 \
    gstreamer-audio-1.0 \
    gstreamer-tag-1.0 \
    gstreamer-pbutils-1.0 \
    gstreamer-video-1.0 \
    gstreamer-gl-1.0
}

macx {
    QMAKE_MAC_SDK = macosx10.9
    INCLUDEPATH += /Library/Frameworks/GStreamer.framework/Headers

    LIBS += \
        -framework AppKit \
        -F/Library/Frameworks -framework GStreamer
}

HEADERS += \
    qgstplayer.h \
    player.h \
    quickrenderer.h \
    imagesample.h

SOURCES += main.cpp \
    qgstplayer.cpp \
    ../lib/gst/player/gstplayer.c \
    ../lib/gst/player/gstplayer-media-info.c \
    player.cpp \
    quickrenderer.cpp \
    imagesample.cpp

DISTFILES +=
