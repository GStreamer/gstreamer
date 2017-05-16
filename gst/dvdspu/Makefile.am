
plugin_LTLIBRARIES = libgstdvdspu.la

libgstdvdspu_la_SOURCES = gstdvdspu.c gstdvdspu-render.c gstspu-vobsub.c gstspu-vobsub-render.c gstspu-pgs.c

libgstdvdspu_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstdvdspu_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
     $(GST_LIBS)
libgstdvdspu_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstdvdspu.h gstspu-pgs.h gstspu-vobsub.h gstspu-common.h

EXTRA_DIST = Notes.txt
