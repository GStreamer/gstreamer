include $(top_srcdir)/common/check.mak

TESTS_ENVIRONMENT = 

plugindir = $(libdir)/gstreamer-@GST_API_VERSION@

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# the core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.*

common_cflags=-I$(top_srcdir) $(GST_PLUGINS_BASE_CFLAGS) $(GST_OBJ_CFLAGS) \
	$(GST_CHECK_CFLAGS) $(GST_OPTION_CFLAGS) $(GST_CFLAGS)
common_ldadd=$(top_builddir)/ges/libges-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) -lgstpbutils-$(GST_API_VERSION) \
	$(GST_OBJ_LIBS) $(GST_CHECK_LIBS)

testutils_noisnt_libraries=libtestutils.la
testutils_noinst_headers=ges/test-utils.h
libtestutils_la_LIBADD=$(common_ldadd)
libtestutils_la_CFLAGS=$(common_cflags)
libtestutils_la_SOURCES=ges/test-utils.c

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
	ges/timelineedition	\
	ges/titles\
	ges/transition	\
	ges/overlays\
	ges/text_properties\
	ges/save_and_load

noinst_LTLIBRARIES=$(testutils_noisnt_libraries)
noinst_HEADERS=$(testutils_noinst_headers)

TESTS = $(check_PROGRAMS)

AM_CFLAGS =  $(common_cflags) -UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS
LDADD = $(common_ldadd) libtestutils.la

EXTRA_DIST = #gst-plugins-bad.supp
