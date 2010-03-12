include $(top_srcdir)/common/check.mak

TESTS_ENVIRONMENT = 

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# the core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.*

SUPPRESSIONS = $(top_srcdir)/common/gst.supp # $(srcdir)/gst-plugins-bad.supp

clean-local: clean-local-check

check_PROGRAMS = \
	ges/basic	\
	ges/filesource	\
	ges/layer	\
	ges/simplelayer	\
	ges/timelineobject

noinst_HEADERS = 

TESTS = $(check_PROGRAMS)

AM_CFLAGS =  -I$(top_srcdir) $(GST_OBJ_CFLAGS) $(GST_CHECK_CFLAGS) $(CHECK_CFLAGS) $(GST_OPTION_CFLAGS)
LDADD = $(top_builddir)/ges/libges-@GST_MAJORMINOR@.la -lges-@GST_MAJORMINOR@ $(GST_OBJ_LIBS) $(GST_CHECK_LIBS) $(CHECK_LIBS) 

EXTRA_DIST = #gst-plugins-bad.supp
