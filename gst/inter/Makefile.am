plugin_LTLIBRARIES = libgstinter.la

noinst_PROGRAMS = gstintertest

libgstinter_la_SOURCES = \
	gstinteraudiosink.c \
	gstinteraudiosrc.c \
	gstintersubsink.c \
	gstintersubsrc.c \
	gstintervideosink.c \
	gstintervideosrc.c \
	gstinter.c \
	gstintersurface.c

noinst_HEADERS = \
	gstinteraudiosink.h \
	gstinteraudiosrc.h \
	gstintersubsink.h \
	gstintersubsrc.h \
	gstintervideosink.h \
	gstintervideosrc.h \
	gstintersurface.h

libgstinter_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstinter_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(LIBM)

libgstinter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstinter_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

gstintertest_SOURCES = \
	gstintertest.c

gstintertest_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS)

gstintertest_LDADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_LIBS) \
	$(LIBM)
