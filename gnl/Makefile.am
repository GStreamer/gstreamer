plugin_LTLIBRARIES = libgnl.la

libgnl_la_SOURCES =		\
	gnl.c			\
	gnlobject.c		\
	gnlcomposition.c	\
	gnlghostpad.c		\
	gnloperation.c		\
	gnlsource.c		\
	gnlurisource.c
libgnl_la_CFLAGS = $(GST_CFLAGS)
libgnl_la_LIBADD = $(GST_LIBS)
libgnl_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgnl_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

gnl_headers =			\
	gnl.h			\
	gnlobject.h		\
	gnlcomposition.h	\
	gnltypes.h		\
	gnlghostpad.h		\
	gnloperation.h		\
	gnlsource.h		\
	gnltypes.h		\
	gnlurisource.h

DISTCLEANFILE = $(CLEANFILES)

#files built on make all/check/instal
BUILT_SOURCES =	$(built_header_configure)

noinst_HEADERS = $(gnl_headers) $(built_header_configure)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgnl -:SHARED libgnl \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgnl_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgnl_la_CFLAGS) \
	 -:LDFLAGS $(libgnl_la_LDFLAGS) \
	           $(libgnl_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-@GST_API_VERSION@' \
	> $@
