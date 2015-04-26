# plugindir is set in configure

plugin_LTLIBRARIES = libgstaudiofx.la

# FIXME 0.11: ignore GValueArray warnings for now until this is sorted
ERROR_CFLAGS=

ORC_SOURCE=audiopanoramaorc
include $(top_srcdir)/common/orc.mak

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
	audioecho.c \
	gstscaletempo.c
nodist_libgstaudiofx_la_SOURCES = $(ORC_NODIST_SOURCES)

# flags used to compile this plugin
libgstaudiofx_la_CFLAGS = $(GST_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(ORC_CFLAGS)
libgstaudiofx_la_LIBADD = $(GST_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) \
	-lgstfft-$(GST_API_VERSION) \
	$(ORC_LIBS) \
	$(LIBM)
libgstaudiofx_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudiofx_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

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
	gstscaletempo.h \
	math_compat.h
