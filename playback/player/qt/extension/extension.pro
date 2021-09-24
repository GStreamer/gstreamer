TEMPLATE = lib
CONFIG += qt plugin
QT += qml quick
CONFIG += c++11
DEFINES += GST_USE_UNSTABLE_API
TARGET = qmlplayerextension

# Additional import path used to resolve QML modules in Qt Creator's code model
# QML_IMPORT_PATH =

HEADERS += qplayerextension.h \
    qgstplayer.h \
    player.h \
    quickrenderer.h \
    imagesample.h

SOURCES += qplayerextension.cpp \
    qgstplayer.cpp \
    player.cpp \
    quickrenderer.cpp \
    imagesample.cpp

unix:!macx {
QT_CONFIG -= no-pkg-config
CONFIG += link_pkgconfig
PKGCONFIG = \
    gstreamer-1.0 \
    gstreamer-player-1.0 \
    gstreamer-tag-1.0
}

macx {
    QMAKE_MAC_SDK = macosx10.9
    INCLUDEPATH += /Library/Frameworks/GStreamer.framework/Headers

    LIBS += \
        -framework AppKit \
        -F/Library/Frameworks -framework GStreamer
}
