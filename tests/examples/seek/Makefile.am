if HAVE_GTK_X11
GTK_EXAMPLES=seek scrubby
endif

examples = $(GTK_EXAMPLES)

noinst_PROGRAMS = $(examples)

LDADD = $(GST_LIBS) $(GTK_LIBS) \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la
AM_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GTK_CFLAGS) -I$(top_builddir)/gst-libs
