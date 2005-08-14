CHECK_REGISTRY=$(top_builddir)/check/test-registry.xml

TESTS_ENVIRONMENT=\
	GST_PLUGIN_PATH_ONLY=yes \
	GST_PLUGIN_PATH=$(top_builddir)/gst \
	GST_REGISTRY=$(CHECK_REGISTRY)

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

# rebuild gst-register-@GST_MAJORMINOR@ if needed
$(top_builddir)/tools/gst-register-@GST_MAJORMINOR@$(EXEEXT):
	cd $(top_builddir)/tools && make

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.xml

EXTRA_DIST = gst.supp

clean-local:
	for i in `find . -name ".libs" -type d`; do \
	  rm -rf $$i; \
	done

$(CHECK_REGISTRY):
	$(TESTS_ENVIRONMENT)					\
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
	elements/gstfakesrc			\
	pipelines/simple_launch_lines		\
	pipelines/cleanup			\
	states/sinks				\
	gst-libs/controller			\
	gst-libs/gdp

check_PROGRAMS = $(TESTS)

noinst_LTLIBRARIES = libgstcheck.la

libgstcheck_la_SOURCES = gstcheck.c
libgstcheck_la_LIBADD = $(GST_OBJ_LIBS)
noinst_HEADERS = gstcheck.h gst/capslist.h

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(CHECK_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(CHECK_LIBS) libgstcheck.la

gst_libs_gdp_SOURCES = \
	gst-libs/gdp.c \
	$(top_srcdir)/libs/gst/dataprotocol/dataprotocol.c
# remove GST_ENABLE_NEW when dataprotocol has been declared API-stable
gst_libs_gdp_CFLAGS = $(AM_CFLAGS) -DGST_ENABLE_NEW

gst_libs_controller_LDADD = $(GST_OBJ_LIBS) $(CHECK_LIBS) libgstcheck.la \
	$(top_builddir)/libs/gst/controller/libgstcontroller-@GST_MAJORMINOR@.la

# valgrind testing

# these just need fixing, period
TESTS_TO_FIX =					\
	gst/gstghostpad				\
	gst/gstiterator				\
	gst/gstmessage				\
	gst/gstsystemclock			\
	gst/gsttag				\
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

if HAVE_VALGRIND
# hangs spectacularly on some machines, so let's not do this by default yet
check-local-disabled:
	make valgrind
else
check-local-disabled:
	@true
endif

# run any given test by running make test.check
%.check: % $(CHECK_REGISTRY)
	@$(TESTS_ENVIRONMENT)					\
	$*


# valgrind any given test by running make test.valgrind
%.valgrind: % $(CHECK_REGISTRY)
	@$(TESTS_ENVIRONMENT)					\
	libtool --mode=execute					\
	$(VALGRIND_PATH) -q --suppressions=$(srcdir)/gst.supp	\
	--tool=memcheck --leak-check=yes --trace-children=yes	\
	$*

# valgrind all tests
valgrind: $(TESTS)
	@echo "Valgrinding tests ..."
	$(TESTS_ENVIRONMENT) $(top_builddir)/tools/gst-register
	@failed=0;							\
	for t in $(filter-out $(VALGRIND_TESTS_DISABLE),$(TESTS)); do	\
		make $$t.valgrind;					\
		if test "$$?" -ne 0; then                               \
                        echo "Valgrind error for test $$t";		\
			failed=`expr $$failed + 1`;			\
                fi;							\
	done;								\
	if test "$$failed" -ne 0; then					\
		echo "$$failed tests had leaks under valgrind";		\
		false;							\
	fi
