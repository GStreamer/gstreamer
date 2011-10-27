
noinst_PROGRAMS = testchannels
testchannels_SOURCES = testchannels.c
testchannels_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
testchannels_LDADD = $(top_builddir)/gst-libs/gst/audio/libgstaudio-$(GST_MAJORMINOR).la \
                     $(GST_LIBS)

if HAVE_GTK
noinst_PROGRAMS = audiomix
audiomix_SOURCES = audiomix.c
audiomix_CFLAGS = $(GST_CFLAGS) $(GTK_CFLAGS) -D_GNU_SOURCE
audiomix_LDADD = $(GST_LIBS) $(GTK_LIBS) $(LIBM)
endif
