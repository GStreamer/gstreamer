plugin_LTLIBRARIES = libgstnle.la

libgstnle_la_SOURCES = gstnle.c \
	nleobject.c		\
	nlecomposition.c	\
	nleghostpad.c		\
	nleoperation.c		\
	nlesource.c		\
	nleurisource.c

libgstnle_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	-I$(top_srcdir)

libgstnle_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) $(GST_LIBS)

libgstnle_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstnle_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	nle.h			\
	nleobject.h		\
	nlecomposition.h	\
	nletypes.h		\
	nleghostpad.h		\
	nleoperation.h		\
	nlesource.h		\
	nletypes.h		\
	nleurisource.h
