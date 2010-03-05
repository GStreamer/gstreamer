plugin_LTLIBRARIES = libgstvaapisink.la

libgstvaapisink_la_SOURCES =	\
	gstvaapisink.c		\
	$(NULL)

noinst_HEADERS =		\
	gstvaapisink.h		\
	$(NULL)

libgstvaapisink_la_CFLAGS =	\
	$(GST_CFLAGS)		\
	$(GST_VIDEO_CFLAGS)	\
	$(GST_PLUGINS_BASE_CFLAGS)

libgstvaapisink_la_LIBADD =	\
	$(GST_LIBS)		\
	$(GST_VIDEO_LIBS)	\
	$(GST_PLUGINS_BASE_LIBS)

libgstvaapisink_la_LIBTOOLFLAGS = --tag=disable-static

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
