plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstcolorspace.la

libgstcolorspace_la_SOURCES = gstcolorspace.c 
libgstcolorspace_la_CFLAGS = $(GST_CFLAGS) $(LCS_CFLAGS)
libgstcolorspace_la_LIBADD = $(LCS_LIBS)
libgstcolorspace_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = 
