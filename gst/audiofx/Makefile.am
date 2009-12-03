# plugindir is set in configure

plugin_LTLIBRARIES = libgstaudiofx.la

# sources used to compile this plug-in
libgstaudiofx_la_SOURCES = audiofx.c\
	audiopanorama.c \
	audioinvert.c \
	audioamplify.c \
	audiodynamic.c \
	audiokaraoke.c \
	audiofxbaseiirfilter.c \
	audiocheblimit.c \
	audiochebband.c \
	audioiirfilter.c \
	audiofxbasefirfilter.c \
	audiowsincband.c \
	audiowsinclimit.c \
	audiofirfilter.c \
	audioecho.c

# flags used to compile this plugin
libgstaudiofx_la_CFLAGS = $(GST_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CONTROLLER_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstaudiofx_la_LIBADD = $(GST_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_CONTROLLER_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_MAJORMINOR) \
	-lgstfft-$(GST_MAJORMINOR) \
	$(LIBM)
libgstaudiofx_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudiofx_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = audiopanorama.h \
	audioinvert.h \
	audioamplify.h \
	audiodynamic.h \
	audiokaraoke.h \
	audiofxbaseiirfilter.h \
	audiocheblimit.h \
	audiochebband.h \
	audioiirfilter.h \
	audiofxbasefirfilter.h \
	audiowsincband.h \
        audiowsinclimit.h \
	audiofirfilter.h \
	audioecho.h \
	math_compat.h

