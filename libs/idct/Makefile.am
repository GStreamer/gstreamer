if HAVE_LIBMMX
GSTIDCTARCH_SRCS = mmxidct.S mmx32idct.c sseidct.S
else
GSTIDCTARCH_SRCS =
endif

filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstidct.la

libgstidct_la_SOURCES = \
        fastintidct.c \
	floatidct.c \
	gstidct.c \
	intidct.c \
	$(GSTIDCTARCH_SRCS)

libgstidctincludedir = $(includedir)/gst/libs/gstidct
libgstidctinclude_HEADERS = gstidct.h

check_PROGRAMS = ieeetest

ieeetest_SOURCES = ieeetest.c
ieeetest_LDADD = libgstidct.la
ieeetest_CFLAGS = $(GNOME_CFLAGS) $(GST_CFLAGS) $(XML_CFLAGS)
ieeetest_LDFLAGS = $(GNOME_LIBS) $(GST_LIBS)

noinst_HEADERS = dct.h

libgstidct_la_LIBADD = $(GST_LIBS)
libgstidct_la_CFLAGS = $(GST_CFLAGS) -finline-functions -ffast-math
