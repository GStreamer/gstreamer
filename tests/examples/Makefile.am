if USE_GTK3
GTK_DIR=gtk
else
GTK_DIR=
endif

if USE_JACK
JACK_DIR=jack
else
JACK_DIR=
endif

if USE_CAIRO
CAIRO_DIR=cairo
else
CAIRO_DIR=
endif

EXTRA_DIST = \
	qt/qmlsrc/main.qml \
	qt/qmlsrc/qml.qrc \
	qt/qmlsrc/grabqml.pro \
	qt/qmlsrc/main.cpp \
	qt/qmlsink/main.qml \
	qt/qmlsink/qml.qrc \
	qt/qmlsink/CMakeLists.txt \
	qt/qmlsink/play.pro \
	qt/qmlsink/main.cpp


SUBDIRS = audiofx equalizer $(GTK_DIR) $(JACK_DIR) level \
	rtp rtsp shapewipe spectrum v4l2 $(CAIRO_DIR)

DIST_SUBDIRS = audiofx equalizer gtk jack level \
	rtp rtsp shapewipe spectrum v4l2 cairo

include $(top_srcdir)/common/parallel-subdirs.mak
