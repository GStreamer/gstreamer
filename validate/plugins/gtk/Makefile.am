plugin_LTLIBRARIES = libgstvalidategtk.la

libgstvalidategtk_la_SOURCES = gstvalidategtk.c

libgstvalidategtk_la_CFLAGS = $(GST_ALL_CFLAGS) $(GTK_CFLAGS)
libgstvalidategtk_la_LIBADD = $(GST_ALL_LIBS) $(top_builddir)/gst/validate/libgstvalidate-@GST_API_VERSION@.la $(GTK_LIBS)
libgstvalidategtk_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_ALL_LDFLAGS) $(GTK_LDFLAGS)

CLEANFILES =


