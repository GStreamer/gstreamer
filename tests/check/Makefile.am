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
PARSE_CHECKS = pipelines/simple-launch-lines pipelines/cleanup pipelines/parse-launch
endif

# if it's calling gst_element_factory_make(), it will probably not work without
# a registry
if GST_DISABLE_REGISTRY
REGISTRY_CHECKS =
else
REGISTRY_CHECKS =				\
	gst/gst				     	\
	gst/gstbin			     	\
	gst/gstelement				\
	gst/gstevent				\
	gst/gstghostpad				\
	gst/gstplugin				\
	gst/gstquery				\
	gst/gstutils				\
	generic/sinks				\
	elements/fakesink			\
	elements/fakesrc			\
	elements/fdsrc		          	\
	elements/identity			\
	libs/basesrc				\
	libs/controller				\
	libs/typefindhelper			\
	pipelines/stress
endif

check_PROGRAMS =				\
	gst/gstabi			     	\
        gst/gstbuffer				\
        gst/gstbus				\
	gst/gstcaps     			\
	gst/gstiterator 			\
	gst/gstmessage	 			\
	gst/gstminiobject 			\
	gst/gstobject				\
	gst/gstpad				\
	gst/gstsegment				\
	gst/gstsystemclock			\
	gst/gststructure			\
	gst/gsttag				\
	gst/gsttask				\
	gst/gstvalue				\
	generic/states				\
	$(PARSE_CHECKS)				\
	$(REGISTRY_CHECKS)			\
	libs/libsabi				\
	libs/gdp				\
	libs/adapter				\
	libs/gstnetclientclock			\
	libs/gstnettimeprovider

# failing tests
noinst_PROGRAMS =				\
	libs/collectpads			\
	gst/gstpipeline

TESTS = $(check_PROGRAMS)

noinst_HEADERS = \
	gst/capslist.h \
	gst/struct_i386.h \
	gst/struct_ppc64.h \
	gst/struct_x86_64.h \
	libs/struct_i386.h \
	libs/struct_ppc64.h \
	libs/struct_x86_64.h

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(CHECK_CFLAGS)
LDADD = $(top_builddir)/libs/gst/check/libgstcheck-@GST_MAJORMINOR@.la \
	$(GST_OBJ_LIBS) \
	$(CHECK_LIBS)

libs_gdp_SOURCES = \
	libs/gdp.c \
	$(top_srcdir)/libs/gst/dataprotocol/dataprotocol.c
libs_gdp_CFLAGS = $(AM_CFLAGS)

elements_fdsrc_CFLAGS=$(GST_OBJ_CFLAGS) $(CHECK_CFLAGS) -DTESTFILE=\"$(top_srcdir)/configure.ac\"

libs_basesrc_LDADD = \
	$(top_builddir)/libs/gst/base/libgstbase-@GST_MAJORMINOR@.la \
	$(LDADD)
libs_adapter_LDADD = \
	$(top_builddir)/libs/gst/base/libgstbase-@GST_MAJORMINOR@.la \
	$(LDADD)
libs_collectpads_LDADD = \
	$(top_builddir)/libs/gst/base/libgstbase-@GST_MAJORMINOR@.la \
	$(LDADD)
libs_controller_LDADD = \
	$(top_builddir)/libs/gst/controller/libgstcontroller-@GST_MAJORMINOR@.la \
	$(LDADD)
libs_gstnetclientclock_LDADD = \
	$(top_builddir)/libs/gst/net/libgstnet-@GST_MAJORMINOR@.la \
	$(LDADD)
libs_gstnettimeprovider_LDADD = \
	$(top_builddir)/libs/gst/net/libgstnet-@GST_MAJORMINOR@.la \
	$(LDADD)
libs_typefindhelper_LDADD = \
	$(top_builddir)/libs/gst/base/libgstbase-@GST_MAJORMINOR@.la \
	$(LDADD)

# valgrind testing
# these just need valgrind fixing, period
VALGRIND_TO_FIX = \
	libs/collectpads

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

# indexers does not get tested yet
COVERAGE_DIRS = \
	gst \
	libs/gst/base \
	libs/gst/controller \
	libs/gst/check \
	libs/gst/dataprotocol \
	libs/gst/net \
	plugins/elements
COVERAGE_FILES = $(foreach dir,$(COVERAGE_DIRS),$(wildcard $(top_builddir)/$(dir)/*.gcov))
COVERAGE_FILES_REL = $(subst $(top_builddir)/,,$(COVERAGE_FILES))
COVERAGE_OUT_FILES = $(foreach dir,$(COVERAGE_DIRS),$(wildcard $(top_builddir)/$(dir)/*.gcov.out))
COVERAGE_OUT_FILES_REL = $(subst $(top_builddir)/,,$(COVERAGE_OUT_FILES))

debug:
	echo $(COVERAGE_FILES)
	echo $(COVERAGE_FILES_REL)

.PHONY: coverage
if GST_GCOV_ENABLED
coverage:
	for file in `find $(top_builddir) -name '*.gcda'`; do rm $$file; done
	make check
	make coverage-report
else
coverage:
	echo "You need to configure with --enable-gcov to get coverage data"
	exit 1
endif

coverage-report:
	rm -r coverage
	for dir in $(COVERAGE_DIRS); do 				\
	  mkdir -p coverage/$$dir; 					\
	  make -C $(top_builddir)/$$dir gcov;			\
        done
	for dir in $(COVERAGE_DIRS); do					\
	    files="`ls $(top_builddir)/$$dir/*.gcov.out 2> /dev/null`"; \
          if test ! -z "$$files"; then					\
	    perl $(top_srcdir)/common/coverage/coverage-report.pl 	\
	      $(top_builddir)/$$dir/*.gcov.out >			\
	      coverage/$$dir/index.xml;					\
	    xsltproc $(top_srcdir)/common/coverage/coverage-report.xsl	\
	      coverage/$$dir/index.xml > coverage/$$dir/index.html;	\
	  fi; \
        done
	
	for file in $(COVERAGE_FILES_REL); do				\
	  echo Generating coverage/$$file.html; 			\
	  perl $(top_srcdir)/common/coverage/coverage-report-entry.pl   \
	    $(top_builddir)/$$file > coverage/$$file.html; 		\
	done
