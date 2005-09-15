include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/check/test-registry.xml

GST_TOOLS_DIR = $(top_builddir)/tools

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
        $(REGISTRY_ENVIRONMENT)                                 \
        GST_PLUGIN_PATH_ONLY=yes                                \
        GST_PLUGIN_PATH=$(top_builddir)/gst/elements/.libs:$(top_builddir)/gst/indexers/.libs

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.xml

SUPPRESSIONS = $(top_srcdir)/common/gst.supp

clean-local: clean-local-check

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
	gst/gstsystemclock			\
	gst/gststructure			\
	gst/gsttag				\
	gst/gstvalue				\
	elements/fakesrc			\
	elements/identity			\
	generic/states				\
	pipelines/simple_launch_lines		\
	pipelines/cleanup			\
	states/sinks				\
	gst-libs/controller			\
	gst-libs/gdp

TESTS = $(check_PROGRAMS)

noinst_HEADERS = gst/capslist.h

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(CHECK_CFLAGS)
LDADD = $(GST_OBJ_LIBS) \
	$(top_builddir)/gst/check/libgstcheck-@GST_MAJORMINOR@.la \
	$(CHECK_LIBS)

gst_libs_gdp_SOURCES = \
	gst-libs/gdp.c \
	$(top_srcdir)/libs/gst/dataprotocol/dataprotocol.c
# remove GST_ENABLE_NEW when dataprotocol has been declared API-stable
gst_libs_gdp_CFLAGS = $(AM_CFLAGS) -DGST_ENABLE_NEW

gst_libs_controller_LDADD = $(GST_OBJ_LIBS) \
	$(top_builddir)/gst/check/libgstcheck-@GST_MAJORMINOR@.la \
	$(CHECK_LIBS) \
	$(top_builddir)/libs/gst/controller/libgstcontroller-@GST_MAJORMINOR@.la 

# valgrind testing
# these just need valgrind fixing, period
TESTS_TO_FIX =					\
	gst-libs/controller			\
	pipelines/cleanup			\
	pipelines/simple_launch_lines

# these need fixing because the threads cause segfaults under valgrind
TESTS_THREADED =				\
	gst/gstminiobject 			\
	gst/gstobject

VALGRIND_TESTS_DISABLE = 					\
	$(TESTS_THREADED)					\
	$(TESTS_TO_FIX)
