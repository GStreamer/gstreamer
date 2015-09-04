noinst_PROGRAMS = audio-example control-sources text-color-example absolute-example

AM_CFLAGS  = $(GST_OBJ_CFLAGS) -I$(top_builddir)/libs

LDADD = \
	$(top_builddir)/libs/gst/controller/libgstcontroller-@GST_API_VERSION@.la \
	$(GST_OBJ_LIBS)

if HAVE_GTK
noinst_PROGRAMS += controller-graph

controller_graph_CFLAGS = $(GST_OBJ_CFLAGS) $(GTK_CFLAGS)
controller_graph_LDADD = \
	$(top_builddir)/libs/gst/controller/libgstcontroller-@GST_API_VERSION@.la \
	$(GST_OBJ_LIBS) $(GTK_LIBS)

endif