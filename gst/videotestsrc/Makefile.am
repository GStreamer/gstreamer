plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstvideotestsrc.la

libgstvideotestsrc_la_SOURCES = \
			gstvideotestsrc.c

libgstvideotestsrc_la_CFLAGS = -O2 $(FOMIT_FRAME_POINTER) -funroll-all-loops -finline-functions -ffast-math $(GST_CFLAGS)
libgstvideotestsrc_la_LIBADD =
libgstvideotestsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstvideotestsrc.h
