SUBDIRS=libmonkeyaudio .

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstmonkeyaudio.la

libgstmonkeyaudio_la_SOURCES = monkey_io.cc gstmonkeyaudio.cc gstmonkeydec.cc gstmonkeyenc.cc
libgstmonkeyaudio_la_CFLAGS = $(GST_CFLAGS)
libgstmonkeyaudio_la_CXXFLAGS = $(GST_CFLAGS)
libgstmonkeyaudio_la_LIBADD = $(top_builddir)/gst/monkeyaudio/libmonkeyaudio/libmonkeyaudio.la
# we add stdc++ because it'll be used by C programs
libgstmonkeyaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lstdc++

noinst_HEADERS = gstmonkeyaudio.h gstmonkeydec.h monkey_io.h gstmonkeyenc.h

# this is the silliest hack I ever did, but for some reason some dependency
# tracking is not able to figure out that the actual file is gstmonkeyaudio.cc
# I appreciate any help into fixing this mess definitively
# I suspect a libtool bug, because in the end it doesn't even *use* the .c
# file; my libtool version when seeing this bug was 1.4.2
gstmonkeyaudio.c: gstmonkeyaudio.cc
	cp $(srcdir)/gstmonkeyaudio.cc gstmonkeyaudio.c
