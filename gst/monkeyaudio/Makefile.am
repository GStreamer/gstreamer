SUBDIRS=libmonkeyaudio .

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstmonkeyaudio.la

libgstmonkeyaudio_la_SOURCES = monkey_io.cc gstmonkeyaudio.cc gstmonkeydec.cc gstmonkeyenc.cc
libgstmonkeyaudio_la_CPPFLAGS = $(GST_CFLAGS)
libgstmonkeyaudio_la_CXXFLAGS = $(libgstmonkeyaudio_la_CPPFLAGS)
libgstmonkeyaudio_la_LIBADD = $(top_builddir)/gst/monkeyaudio/libmonkeyaudio/libmonkeyaudio.la
libgstmonkeyaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmonkeyaudio.h gstmonkeydec.h monkey_io.h gstmonkeyenc.h
