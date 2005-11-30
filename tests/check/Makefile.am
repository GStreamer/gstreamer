include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.xml

GST_TOOLS_DIR = $(top_builddir)/tools

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
        $(REGISTRY_ENVIRONMENT)                                 \
        GST_PLUGIN_SYSTEM_PATH=					\
        GST_PLUGIN_PATH=$(top_builddir)/plugins

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# the core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.xml

SUPPRESSIONS = $(top_srcdir)/common/gst.supp

clean-local: clean-local-check

# checks to fix
#	gst/gstplugin

if GST_DISABLE_PARSE
PARSE_CHECKS =
else
PARSE_CHECKS = pipelines/simple-launch-lines pipelines/cleanup
endif

check_PROGRAMS =				\
	gst/gst				     	\
	gst/gstbin			     	\
        gst/gstbuffer				\
        gst/gstbus				\
	gst/gstcaps     			\
	gst/gstelement				\
	gst/gstevent				\
	gst/gstghostpad				\
	gst/gstiterator 			\
	gst/gstmessage	 			\
	gst/gstminiobject 			\
	gst/gstobject				\
	gst/gstpad				\
	gst/gstplugin				\
	gst/gstsegment				\
	gst/gstsystemclock			\
	gst/gststructure			\
	gst/gsttag				\
	gst/gstutils				\
	gst/gstvalue				\
	elements/fakesrc			\
	elements/identity			\
	elements/fdsrc		          	\
	generic/sinks				\
	generic/states				\
	$(PARSE_CHECKS)				\
	pipelines/stress			\
	libs/controller				\
	libs/gdp				\
	libs/gstnetclientclock			\
	libs/gstnettimeprovider

# failing tests
noinst_PROGRAMS =				\
	gst/gstpipeline

TESTS = $(check_PROGRAMS)

noinst_HEADERS = gst/capslist.h

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(CHECK_CFLAGS)
LDADD = $(top_builddir)/libs/gst/check/libgstcheck-@GST_MAJORMINOR@.la \
	$(GST_OBJ_LIBS) \
	$(CHECK_LIBS)

libs_gdp_SOURCES = \
	libs/gdp.c \
	$(top_srcdir)/libs/gst/dataprotocol/dataprotocol.c
libs_gdp_CFLAGS = $(AM_CFLAGS)

elements_fdsrc_CFLAGS=$(GST_OBJ_CFLAGS) $(CHECK_CFLAGS) -DTESTFILE=\"$(top_srcdir)/configure.ac\"

libs_controller_LDADD = \
	$(top_builddir)/libs/gst/controller/libgstcontroller-@GST_MAJORMINOR@.la \
	$(LDADD)

libs_gstnetclientclock_LDADD = \
	$(top_builddir)/libs/gst/net/libgstnet-@GST_MAJORMINOR@.la \
	$(LDADD)
libs_gstnettimeprovider_LDADD = \
	$(top_builddir)/libs/gst/net/libgstnet-@GST_MAJORMINOR@.la \
	$(LDADD)

# valgrind testing
# these just need valgrind fixing, period
VALGRIND_TO_FIX =

VALGRIND_IGNORE = \
	pipelines/stress

# these need fixing because the threads cause segfaults under valgrind
TESTS_THREADED =				\
	gst/gstminiobject 			\
	gst/gstobject

VALGRIND_TESTS_DISABLE = 					\
	$(TESTS_THREADED)					\
	$(VALGRIND_IGNORE)					\
	$(VALGRIND_TO_FIX)
