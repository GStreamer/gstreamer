#PANGO_CFLAGS = -I/usr/include/pango-1.0 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -I/usr/include/freetype2
#PANGO_LDFLAGS = -Wl,--export-dynamic -lpangoft2-1.0 -lpango-1.0 -lgobject-2.0 -lgmodule-2.0 -ldl -lglib-2.0

plugin_LTLIBRARIES = libgsttimeoverlay.la libgsttextoverlay.la

noinst_HEADERS = gsttimeoverlay.h gsttextoverlay.h

libgsttimeoverlay_la_SOURCES = gsttimeoverlay.c
libgsttimeoverlay_la_CFLAGS = $(GST_CFLAGS) $(PANGO_CFLAGS) -I$(top_srcdir)/gst/videofilter
libgsttimeoverlay_la_LIBADD = $(GST_LIBS) $(PANGO_LIBS) -lm
libgsttimeoverlay_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgsttextoverlay_la_SOURCES = gsttextoverlay.c
libgsttextoverlay_la_CFLAGS = $(GST_CFLAGS) $(PANGO_CFLAGS)
libgsttextoverlay_la_LIBADD = $(GST_LIBS) $(PANGO_LIBS)
libgsttextoverlay_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
