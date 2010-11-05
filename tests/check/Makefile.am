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
	ges/backgroundsource\
	ges/basic	\
	ges/layer	\
	ges/effects	\
	ges/filesource	\
	ges/simplelayer	\
	ges/timelineobject	\
	ges/titles\
	ges/transition	\
	ges/overlays\
	ges/text_properties\
	ges/save_and_load

noinst_HEADERS = 

TESTS = $(check_PROGRAMS)

AM_CFLAGS =  -I$(top_srcdir) $(GST_PLUGINS_BASE_CFLAGS) $(GST_OBJ_CFLAGS) \
	$(GST_CHECK_CFLAGS) $(GST_OPTION_CFLAGS) $(GST_CFLAGS) \
	-UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS

LDADD = $(top_builddir)/ges/libges-@GST_MAJORMINOR@.la \
	$(GST_PLUGINS_BASE_LIBS) -lgstpbutils-$(GST_MAJORMINOR) \
	$(GST_OBJ_LIBS) $(GST_CHECK_LIBS)

EXTRA_DIST = #gst-plugins-bad.supp
