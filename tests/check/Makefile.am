TESTS_ENVIRONMENT=\
	GST_PLUGIN_PATH=$(top_builddir)/gst:$(top_builddir)/check \
	GST_REGISTRY=$(top_builddir)/check/test-registry.xml

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

# make all tests depend on the versioned gst-register
$(TESTS): $(top_builddir)/tools/gst-register-@GST_MAJORMINOR@

# rebuild gst-register-@GST_MAJORMINOR@ if needed
$(top_builddir)/tools/gst-register-@GST_MAJORMINOR@:
	cd $(top_builddir)/tools && make

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# This rule is here so make distcheck works on machines where core
# dumps have PIDs appended
CLEANFILES = core.*

TESTS = $(top_builddir)/tools/gst-register-@GST_MAJORMINOR@ \
	gst/gstbin	     	\
    gst/gstbus          \
	gst/gstcaps     	\
	gst/gstdata		\
	gst/gstiterator 	\
	gst/gstobject		\
	gst/gstsystemclock	\
	gst-libs/gdp	

check_PROGRAMS = $(TESTS)

noinst_LTLIBRARIES = libgstcheck.la

libgstcheck_la_SOURCES = gstcheck.c
libgstcheck_la_LIBADD = $(GST_OBJ_LIBS)
noinst_HEADERS = gstcheck.h

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(CHECK_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(CHECK_LIBS) libgstcheck.la

gst_libs_gdp_SOURCES = \
	gst-libs/gdp.c \
	$(top_srcdir)/libs/gst/dataprotocol/dataprotocol.c
# remove GST_ENABLE_NEW when dataprotocol has been declared API-stable
gst_libs_gdp_CFLAGS = $(AM_CFLAGS) -DGST_ENABLE_NEW
