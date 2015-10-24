validateplugin_LTLIBRARIES = libgstvalidatessim.la

libgstvalidatessim_la_SOURCES = gstvalidatessim.c

libgstvalidatessim_la_CFLAGS = $(GST_ALL_CFLAGS) -I$(top_builddir)/gst-libs/gst/video/ $(CAIRO_CFLAGS) $(GST_VIDEO_CFLAGS)
libgstvalidatessim_la_LIBADD = $(GST_ALL_LIBS) $(top_builddir)/gst/validate/libgstvalidate-@GST_API_VERSION@.la $(top_builddir)/gst-libs/gst/video/libgstvalidatevideo-@GST_API_VERSION@.la $(CAIRO_LIBS) $(GST_VIDEO_LIBS)
libgstvalidatessim_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_ALL_LDFLAGS) $(CAIR_LDFLAGS) $(GST_VIDEO_LDFLAGS)

CLEANFILES =



