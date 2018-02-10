
noinst_PROGRAMS = gtksink

gtksink_SOURCES = gtksink.c
gtksink_CFLAGS = $(GTK3_CFLAGS) \
	$(GST_CFLAGS)
gtksink_LDADD = $(GTK3_LIBS) \
	$(GST_LIBS)

if USE_GTK3_GL
noinst_PROGRAMS += gtkglsink glliveshader

gtkglsink_SOURCES = gtkglsink.c
gtkglsink_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_GL_CFLAGS) \
	$(GST_CFLAGS) \
	$(GL_CFLAGS) \
	$(GTK3_CFLAGS)
gtkglsink_LDADD = \
	$(GST_GL_LIBS) \
	$(GST_LIBS) \
	$(GTK3_LIBS) \
	$(X_LIBS)

glliveshader_SOURCES = glliveshader.c
glliveshader_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_GL_CFLAGS) \
	$(GST_CFLAGS) \
	$(GTK3_CFLAGS)
glliveshader_LDADD = \
	$(GST_GL_LIBS) \
	$(GST_LIBS) \
	$(GTK3_LIBS) \
	$(X_LIBS)
endif
