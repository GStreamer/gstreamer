include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/check/test-registry.xml

GST_TOOLS_DIR = $(top_builddir)/tools

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
        $(REGISTRY_ENVIRONMENT)                                 \
        GST_PLUGIN_SYSTEM_PATH=					\
        GST_PLUGIN_PATH=$(top_builddir)/gst/elements:$(top_builddir)/gst/indexers

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.xml

SUPPRESSIONS = $(top_srcdir)/common/gst.supp

clean-local: clean-local-check

# checks to fix
#	gst/gstplugin

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
	gst/gstpipeline				\
	gst/gstplugin				\
	gst/gstsystemclock			\
	gst/gststructure			\
	gst/gsttag				\
	gst/gstutils				\
	gst/gstvalue				\
	elements/fakesrc			\
	elements/identity			\
	generic/states				\
	pipelines/simple_launch_lines		\
	pipelines/stress			\
	pipelines/cleanup			\
	states/sinks				\
	gst-libs/controller			\
	gst-libs/gdp				\
	net/gstnettimeprovider

TESTS = $(check_PROGRAMS)

noinst_HEADERS = gst/capslist.h

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(CHECK_CFLAGS)
LDADD = $(top_builddir)/gst/check/libgstcheck-@GST_MAJORMINOR@.la \
	$(GST_OBJ_LIBS) \
	$(CHECK_LIBS)

gst_libs_gdp_SOURCES = \
	gst-libs/gdp.c \
	$(top_srcdir)/libs/gst/dataprotocol/dataprotocol.c
# remove GST_ENABLE_NEW when dataprotocol has been declared API-stable
# FIXME: time to do this
gst_libs_gdp_CFLAGS = $(AM_CFLAGS) -DGST_ENABLE_NEW

gst_libs_controller_LDADD = \
	$(top_builddir)/libs/gst/controller/libgstcontroller-@GST_MAJORMINOR@.la \
	$(LDADD)

net_gstnettimeprovider_LDADD = \
	$(top_builddir)/gst/net/libgstnet-tempname-@GST_MAJORMINOR@.la \
	$(LDADD)

# valgrind testing
# these just need valgrind fixing, period
VALGRIND_TO_FIX =

# these need fixing because the threads cause segfaults under valgrind
TESTS_THREADED =				\
	gst/gstminiobject 			\
	gst/gstobject

VALGRIND_TESTS_DISABLE = 					\
	$(TESTS_THREADED)					\
	$(VALGRIND_TO_FIX)
