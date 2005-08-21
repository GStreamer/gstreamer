include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/check/test-registry.xml

GST_TOOLS_DIR = $(top_builddir)/tools

TESTS_ENVIRONMENT=\
	GST_REGISTRY=$(CHECK_REGISTRY)

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

# rebuild gst-register-@GST_MAJORMINOR@ if needed
$(top_builddir)/tools/gst-register-@GST_MAJORMINOR@$(EXEEXT):
	cd $(top_builddir)/tools && make

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.xml

SUPPRESSIONS = $(top_srcdir)/common/gst.supp

clean-local: clean-local-check

$(CHECK_REGISTRY):
	$(TESTS_ENVIRONMENT)					\
	GST_PLUGIN_PATH_ONLY=yes				\
	GST_PLUGIN_PATH=$(top_builddir)/gst			\
	$(top_builddir)/tools/gst-register

# FIXME: it'd be nicer to run the versioned register, but in that case
# "make dist" complains about not finding gst-register-0.9.c
TESTS = $(top_builddir)/tools/gst-register 	\
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
	gst/gstsystemclock			\
	gst/gststructure			\
	gst/gsttag				\
	gst/gstvalue				\
	elements/fakesrc			\
	elements/identity			\
	pipelines/simple_launch_lines		\
	pipelines/cleanup			\
	states/sinks				\
	gst-libs/controller			\
	gst-libs/gdp

check_PROGRAMS = $(TESTS)

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

VALGRIND_TESTS_DISABLE = 			\
	$(top_builddir)/tools/gst-register 	\
	$(TESTS_THREADED)			\
	$(TESTS_TO_FIX)
