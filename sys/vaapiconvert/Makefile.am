plugin_LTLIBRARIES = libgstvaapiconvert.la

libgstvaapiconvert_la_SOURCES =	\
	gstvaapiconvert.c	\
	$(NULL)

noinst_HEADERS =		\
	gstvaapiconvert.h	\
	$(NULL)

libgstvaapiconvert_la_CFLAGS =	\
	$(GST_CFLAGS)		\
	$(GST_BASE_CFLAGS)	\
	$(GST_PLUGINS_BASE_CFLAGS)

libgstvaapiconvert_la_LIBADD =	\
	$(GST_LIBS)		\
	$(GST_BASE_LIBS)	\
	$(GST_PLUGINS_BASE_LIBS)

libgstvaapiconvert_la_LIBTOOLFLAGS = --tag=disable-static

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
