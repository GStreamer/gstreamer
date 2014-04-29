TEMPLATE = app
TARGET = videooverlay
DESTDIR = ./Debug
CONFIG += debug
DEFINES += UNICODE QT_THREAD_SUPPORT QT_CORE_LIB QT_GUI_LIB
QT += gui widgets

win32 {
DEFINES += WIN32
INCLUDEPATH += ./GeneratedFiles \
    ./GeneratedFiles/Debug \
    C:/gstreamer/include \
    C:/gstreamer/include/libxml2 \
    C:/gstreamer/include/glib-2.0 \
    C:/gstreamer/lib/glib-2.0/include \
    C:/gstreamer/include/gstreamer-1.0
LIBS += -L"C:/gstreamer/bin" \
    -L"C:/gstreamer/lib" \
    -lgstreamer-1.0 \
    -lgstvideo-1.0 \
    -lglib-2.0 \
    -lgmodule-2.0 \
    -lgobject-2.0 \
    -lgthread-2.0
}

unix {
DEFINES += UNIX
INCLUDEPATH += GeneratedFiles \
    GeneratedFiles/Debug \
    /usr/include/gstreamer-1.0 \
    /usr/local/include/gstreamer-1.0 \
    /usr/include/glib-2.0 \
    /usr/lib/glib-2.0/include \
    /usr/include/libxml2
LIBS += -lgstreamer-1.0 \
    -lgstvideo-1.0 \
	-lglib-2.0 \
	-lgmodule-2.0 \
	-lgobject-2.0 \
	-lgthread-2.0 \
	-lGLU \
	-lGL
}

DEPENDPATH += .
MOC_DIR += ./GeneratedFiles/debug
OBJECTS_DIR += debug
UI_DIR += ./GeneratedFiles
RCC_DIR += ./GeneratedFiles

#Include file(s)
include(videooverlay.pri)
