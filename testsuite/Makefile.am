INCLUDES = \
	$(PYTHON_INCLUDES) \
	$(PYGTK_CFLAGS) \
	$(PYGST_CFLAGS) \
	$(GST_CFLAGS)

noinst_LTLIBRARIES = testhelper.la
linked_LIBS = testhelper.la

testhelper_la_LDFLAGS = -module -avoid-version
testhelper_la_LIBADD = $(GLIB_LIBS)
testhelper_la_SOURCES = \
	testhelpermodule.c \
	test-object.c

# This is a hack to make sure a shared library is built
testhelper.la: $(testhelper_la_OBJECTS) $(testhelper_la_DEPENDENCIES)
	$(LINK) -rpath $(pkgpyexecdir) $(testhelper_la_LDFLAGS) $(testhelper_la_OBJECTS) $(testhelper_la_LIBADD) $(LIBS)

tests =  \
	test_bin.py \
	test_buffer.py \
        test_caps.py \
	test_element.py \
	test_event.py \
	test_ghostpad.py \
        test_interface.py \
	test_message.py \
	test_pad.py \
        test_pipeline.py \
        test_registry.py \
        test_struct.py \
	test_xml.py

check-local: testhelper.la
	@PYTHONPATH=$(top_builddir):$(top_builddir)/gst/.libs:`pwd`:$(top_srcdir):$(PYTHONPATH) $(PYTHON) $(srcdir)/runtests.py
	@rm -fr *.pyc

check-verbose: testhelper.la
	@VERBOSE=yes PYTHONPATH=$(top_builddir):$(top_builddir)/gst/.libs:`pwd`:$(top_srcdir):$(PYTHONPATH) $(PYTHON) $(srcdir)/runtests.py
	@rm -fr *.pyc

EXTRA_DIST = $(tests) common.py runtests.py test-object.h python.supp gstpython.supp

if HAVE_VALGRIND
check-valgrind:
	make valgrind
else
check-valgrind:
	@true
endif

GSTSUPP = $(top_srcdir)/common/gst.supp
PYTHONSUPP = $(top_srcdir)/testsuite/python.supp
GSTPYTHONSUPP = $(top_srcdir)/testsuite/gstpython.supp
TESTS_ENVIRONMENT = PYTHONPATH=$(top_srcdir)/:$(PYTHONPATH)

# gdb any given test_x.py by running make test_x.py.gdb
%.gdb: %
	$(TESTS_ENVIRONMENT)                                    \
	gdb --args 						\
	$(PYTHON) 						\
	$*

# valgrind any given test_x.py by running make test_x.py.valgrind
%.valgrind: %
	$(TESTS_ENVIRONMENT)                                    \
	$(VALGRIND_PATH) -q					\
	--suppressions=$(GSTSUPP)				\
	--suppressions=$(PYTHONSUPP)				\
	--suppressions=$(GSTPYTHONSUPP)				\
	--tool=memcheck --leak-check=yes --trace-children=yes  \
	--leak-resolution=med --num-callers=50		\
	$(PYTHON)						\
	$* 2>&1 | tee valgrind.log
	@if grep "tely lost" valgrind.log; then                 \
	    rm valgrind.log;                                    \
	    exit 1;                                             \
	fi
	@rm valgrind.log

# valgrind all tests
valgrind: $(tests)
	@echo "Valgrinding tests ..."
	@failed=0;                                                      \
	for t in $(filter-out $(VALGRIND_TESTS_DISABLE),$(tests)); do   \
	        make $$t.valgrind;                                      \
	        if test "$$?" -ne 0; then                               \
	                echo "Valgrind error for test $$t";             \
	                failed=`expr $$failed + 1`;                     \
	                whicht="$$whicht $$t";                          \
	        fi;                                                     \
	done;                                                           \
	if test "$$failed" -ne 0; then                                  \
	        echo "$$failed tests had leaks under valgrind:";        \
	        echo "$$whicht";                                        \
	        false;                                                  \
	fi
