include $(top_srcdir)/common/check.mak

CHECK_REGISTRY = $(top_builddir)/tests/check/test-registry.reg

GST_TOOLS_DIR = $(top_builddir)/tools

REGISTRY_ENVIRONMENT = \
	GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
        GST_STATE_IGNORE_ELEMENTS="$(STATE_IGNORE_ELEMENTS)"	\
        $(REGISTRY_ENVIRONMENT)					\
        GST_PLUGIN_SCANNER=$(top_builddir)/libs/gst/helpers/gst-plugin-scanner \
        GST_PLUGIN_SYSTEM_PATH=					\
        GST_PLUGIN_PATH=$(top_builddir)/plugins

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# the core dumps of some machines have PIDs appended, test registry and
# profiling data
CLEANFILES = core core.* test-registry.* *.gcno *.gcda

SUPPRESSIONS = $(top_srcdir)/common/gst.supp

clean-local: clean-local-check

if GST_DISABLE_PARSE
PARSE_CHECKS = pipelines/parse-disabled
else
PARSE_CHECKS = pipelines/simple-launch-lines pipelines/cleanup pipelines/parse-launch
endif

# Do not run the abi test in case any option which causes the API to change has
# been used
if !GST_DISABLE_LOADSAVE
if !GST_DISABLE_REGISTRY
if !GST_DISABLE_TRACE
ABI_CHECKS = gst/gstabi
endif
endif
endif

if HAVE_CXX
CXX_CHECKS = gst/gstcpp libs/gstlibscpp
else
CXX_CHECKS =
endif

# if it's calling gst_element_factory_make(), it will probably not work without
# a registry
if GST_DISABLE_REGISTRY
REGISTRY_CHECKS =
else
REGISTRY_CHECKS =				\
	gst/gst					\
	gst/gstbin				\
	gst/gstchildproxy			\
	gst/gstelement				\
	gst/gstelementfactory			\
	gst/gstevent				\
	gst/gstghostpad				\
	gst/gstindex				\
	gst/gstinterface				\
	gst/gstplugin				\
	gst/gstpreset				\
	gst/gstquery				\
	gst/gstregistry				\
	gst/gsturi  				\
	gst/gstutils				\
	generic/sinks				\
	elements/capsfilter			\
	elements/fakesink			\
	elements/fakesrc			\
	elements/fdsrc			  	\
	elements/filesink			\
	elements/filesrc			\
	elements/funnel				\
	elements/identity			\
	elements/multiqueue			\
	elements/selector			\
	elements/tee			  	\
	elements/queue                          \
	elements/queue2                         \
	elements/valve                          \
	libs/basesrc				\
	libs/basesink				\
	libs/controller				\
	libs/typefindhelper			\
	pipelines/stress			\
	pipelines/queue-error
endif

check_PROGRAMS =				\
	$(ABI_CHECKS)			     	\
	gst/gstatomicqueue			\
	gst/gstbuffer				\
	gst/gstbufferlist			\
	gst/gstbus				\
	gst/gstcaps     			\
	$(CXX_CHECKS)			     	\
	gst/gstdatetime     			\
	gst/gstinfo		 		\
	gst/gstiterator 			\
	gst/gstmessage	 			\
	gst/gstminiobject 			\
	gst/gstobject				\
	gst/gstpad				\
	gst/gstparamspecs			\
	gst/gstpoll     			\
	gst/gstsegment				\
	gst/gstsystemclock			\
	gst/gstclock				\
	gst/gststructure			\
	gst/gsttag				\
	gst/gsttagsetter			\
	gst/gsttask				\
	gst/gstvalue				\
	generic/states				\
	$(PARSE_CHECKS)				\
	$(REGISTRY_CHECKS)			\
	libs/libsabi				\
	libs/gdp				\
	libs/adapter				\
	libs/bitreader				\
	libs/bytereader				\
	libs/bytewriter				\
	libs/gstnetclientclock			\
	libs/gstnettimeprovider			\
	libs/transform1

# failing tests
noinst_PROGRAMS =				\
	gst/gstpipeline				\
	libs/collectpads

# elements to ignore for the state tests
# STATE_IGNORE_ELEMENTS =

TESTS = $(check_PROGRAMS)

noinst_HEADERS = \
	gst/capslist.h \
	gst/struct_arm.h \
	gst/struct_i386.h \
	gst/struct_hppa.h \
	gst/struct_ppc32.h \
	gst/struct_ppc64.h \
	gst/struct_sparc.h \
	gst/struct_x86_64.h \
	libs/struct_arm.h \
	libs/struct_i386.h \
	libs/struct_hppa.h \
	libs/struct_ppc32.h \
	libs/struct_ppc64.h \
	libs/struct_sparc.h \
	libs/struct_x86_64.h

EXTRA_DIST = \
	libs/test_transform.c

AM_CFLAGS = $(GST_OBJ_CFLAGS) -UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS
AM_CXXFLAGS = $(GST_OBJ_CXXFLAGS) -UG_DISABLE_ASSERT -UG_DISABLE_CAST_CHECKS
LDADD = $(top_builddir)/libs/gst/check/libgstcheck-@GST_MAJORMINOR@.la \
	$(top_builddir)/libs/gst/base/libgstbase-@GST_MAJORMINOR@.la \
	$(GST_OBJ_LIBS)

gst_gstcpp_SOURCES = gst/gstcpp.cc

libs_gstlibscpp_SOURCES = libs/gstlibscpp.cc

gst_gstutils_LDADD = $(LDADD) $(GSL_LIBS) $(GMP_LIBS)

libs_gdp_SOURCES = \
	libs/gdp.c
libs_gdp_LDADD = \
	$(top_builddir)/libs/gst/dataprotocol/libgstdataprotocol-@GST_MAJORMINOR@.la \
	$(LDADD)

elements_fdsrc_CFLAGS=$(GST_OBJ_CFLAGS) $(AM_CFLAGS) \
	-DTESTFILE=\"$(top_srcdir)/configure.ac\"
elements_filesrc_CFLAGS=$(GST_OBJ_CFLAGS) $(AM_CFLAGS) \
	-DTESTFILE=\"$(top_srcdir)/configure.ac\"

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
VALGRIND_TO_FIX = \
	gst/gstinfo \
	libs/collectpads \
	pipelines/parse-launch

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
# we rebuild a registry and do gst-inspect so that all the get/set codepaths
# are also covered
coverage:
	for file in `find $(top_builddir) -name '*.gcda'`; do rm $$file; done
	-rm $(CHECK_REGISTRY)
	echo "Inspecting all elements"
	for e in `$(GST_INSPECT) | head -n -2 | cut -d: -f2`; do $(GST_INSPECT) $$e > /dev/null 2>&1; done
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
	  make -C $(top_builddir)/$$dir gcov;				\
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
