if HAVE_GTK_X11
GTK_EXAMPLES=seek scrubby
endif

examples = $(GTK_EXAMPLES) stepping stepping2

noinst_PROGRAMS = $(examples)

LDADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
	$(GST_LIBS) $(GTK_LIBS)

AM_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GTK_CFLAGS) -I$(top_builddir)/gst-libs
