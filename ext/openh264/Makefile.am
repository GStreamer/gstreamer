plugin_LTLIBRARIES = libgstopenh264.la

libgstopenh264_la_SOURCES = \
    gstopenh264plugin.c \
    gstopenh264enc.cpp \
    gstopenh264dec.cpp

libgstopenh264_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(OPENH264_CFLAGS)
libgstopenh264_la_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) $(OPENH264_CFLAGS)
libgstopenh264_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-1.0 $(GST_LIBS) $(OPENH264_LIBS)
libgstopenh264_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
    gstopenh264dec.h \
    gstopenh264enc.h
