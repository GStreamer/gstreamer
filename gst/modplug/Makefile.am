SUBDIRS=libmodplug .

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstmodplug.la

libgstmodplug_la_SOURCES = modplug_types.cc gstmodplug.cc
libgstmodplug_la_CFLAGS = $(GST_CFLAGS)
libgstmodplug_la_CXXFLAGS = $(GST_CFLAGS)
libgstmodplug_la_LIBADD = $(top_builddir)/gst/modplug/libmodplug/libmodplug.la
# we add stdc++ because it'll be used by C programs
libgstmodplug_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lstdc++

noinst_HEADERS = gstmodplug.h modplug_types.h

# this is the silliest hack I ever did, but for some reason some dependency
# tracking is not able to figure out that the actual file is gstmodplug.cc
# I appreciate any help into fixing this mess definitively
# I suspect a libtool bug, because in the end it doesn't even *use* the .c
# file; my libtool version when seeing this bug was 1.4.2
gstmodplug.c: gstmodplug.cc
	cp $(srcdir)/gstmodplug.cc gstmodplug.c

modplug_types.c: modplug_types.cc
	cp $(srcdir)/modplug_types.cc modplug_types.c
