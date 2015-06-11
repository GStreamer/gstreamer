
noinst_PROGRAMS = gtksink

gtksink_SOURCES = gtksink.c
gtksink_CFLAGS = $(GTK3_CFLAGS) \
	$(GST_CFLAGS)
gtksink_LDADD = $(GTK3_LIBS) \
	$(GST_LIBS)

if USE_GTK3_GL
if USE_GL
noinst_PROGRAMS += gtkglsink

gtkglsink_SOURCES = gtkglsink.c
gtkglsink_CFLAGS = $(GTK3_CFLAGS) \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GL_CFLAGS)
gtkglsink_LDADD = $(GTK3_LIBS) \
	$(GST_LIBS) \
	$(GL_LIBS)
endif
endif
