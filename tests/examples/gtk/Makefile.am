
noinst_PROGRAMS = gtksink

gtksink_SOURCES = gtksink.c
gtksink_CFLAGS = $(GTK3_CFLAGS) \
	$(GST_CFLAGS)
gtksink_LDADD = $(GTK3_LIBS) \
	$(GST_LIBS)

if USE_GTK3_GL
if USE_GL
noinst_PROGRAMS += gtkglsink glliveshader

gtkglsink_SOURCES = gtkglsink.c
gtkglsink_CFLAGS = $(GTK3_CFLAGS) \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GL_CFLAGS) \
	-DGST_USE_UNSTABLE_API
gtkglsink_LDADD = $(GTK3_LIBS) \
	$(GST_LIBS) \
	$(GL_LIBS) \
	$(X11_LIBS)

glliveshader_SOURCES = glliveshader.c
glliveshader_CFLAGS = $(GTK3_CFLAGS) \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GL_CFLAGS) \
	-DGST_USE_UNSTABLE_API
glliveshader_LDADD = $(GTK3_LIBS) \
	$(GST_LIBS) \
	$(GL_LIBS) \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_API_VERSION).la \
	$(X11_LIBS)
endif
endif
