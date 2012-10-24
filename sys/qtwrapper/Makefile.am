plugin_LTLIBRARIES = libgstqtwrapper.la

# sources used to compile this plug-in
libgstqtwrapper_la_SOURCES = \
	qtwrapper.c		\
	qtutils.c		\
	codecmapping.c		\
	audiodecoders.c		\
	videodecoders.c		\
	imagedescription.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstqtwrapper_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstqtwrapper_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstqtwrapper_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -Wl,-framework,CoreAudio,-framework,AudioToolbox,-framework,Carbon,-framework,QuickTime,-framework,QuartzCore
libgstqtwrapper_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = 		\
	codecmapping.h		\
	qtwrapper.h		\
	qtutils.h		\
	imagedescription.h
