plugin_LTLIBRARIES = libgstdebugutilsbad.la

libgstdebugutilsbad_la_SOURCES = \
	gstdebugspy.c \
	debugutilsbad.c \
        fpsdisplaysink.c \
        gstchecksumsink.c \
	gstchopmydata.c \
	gstcompare.c \
	gstwatchdog.c \
	gsterrorignore.c

libgstdebugutilsbad_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstdebugutilsbad_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	$(GST_LIBS)
libgstdebugutilsbad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = fpsdisplaysink.h \
	gstchecksumsink.h \
	gstchopmydata.h \
	gstcompare.h \
	gstdebugspy.h \
	gstwatchdog.h \
	gsterrorignore.h
