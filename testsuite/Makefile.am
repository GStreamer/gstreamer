# This is a hack to make sure a shared library is built
tests =  \
	test_adapter.py \
	test_audio.py \
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
	test_segment.py \
	test_taglist.py \
	test_xml.py \
	test_pbutils.py	\
	test_gst.py

EXTRA_DIST = $(tests) common.py cleanup.py runtests.py test-object.h python.supp gstpython.supp

if HAVE_VALGRIND
check-valgrind:
	$(PYTHON) cleanup.py
	make valgrind
else
check-valgrind:
	@true
endif

GSTSUPP = $(top_srcdir)/common/gst.supp
PYTHONSUPP = $(top_srcdir)/testsuite/python.supp
GSTPYTHONSUPP = $(top_srcdir)/testsuite/gstpython.supp
TESTS_ENVIRONMENT = LC_ALL=C

# gdb any given test_x.py by running make test_x.py.gdb
%.gdb: %
	$(TESTS_ENVIRONMENT)                                    \
	gdb --args 						\
	$(PYTHON) 						\
	$*

# valgrind any given test_x.py by running make test_x.py.valgrind
%.valgrind: %
	$(TESTS_ENVIRONMENT)                                    \
	G_DEBUG=gc-friendly G_SLICE=always-malloc					\
	$(VALGRIND_PATH) -q					\
	--suppressions=$(GSTSUPP)				\
	--suppressions=$(PYTHONSUPP)				\
	--suppressions=$(GSTPYTHONSUPP)				\
	--tool=memcheck --leak-check=full --trace-children=yes  \
	--leak-resolution=high --num-callers=50		\
	$(PYTHON)						\
	$* 2>&1 | tee valgrind.log
	@if grep "tely lost" valgrind.log; then                 \
	    rm valgrind.log;                                    \
	    exit 1;                                             \
	fi
	@rm valgrind.log

# valgrind any given test_x.py by running make test_x.py.valgrind
%.gen-suppressions: %
	$(TESTS_ENVIRONMENT)                                    \
	G_DEBUG=gc-friendly G_SLICE=always-malloc					\
	$(VALGRIND_PATH) -q					\
	--suppressions=$(GSTSUPP)				\
	--suppressions=$(PYTHONSUPP)				\
	--suppressions=$(GSTPYTHONSUPP)				\
	--tool=memcheck --leak-check=full --trace-children=yes  \
	--leak-resolution=high --num-callers=50			\
	--gen-suppressions=all					\
	$(PYTHON)						\
	$* 2>&1 | tee valgrind.log
	@if grep "tely lost" valgrind.log; then                 \
	    rm valgrind.log;                                    \
	    exit 1;                                             \
	fi
	@rm valgrind.log

%.check: %
	$(PYTHON) $(srcdir)/cleanup.py
	$(PYTHON) $(srcdir)/runtests.py $*
	@rm -fr *.pyc

%.forever: %
	$(srcdir)/cleanup.py
	@while true; do \
	$(PYTHON) $(srcdir)/runtests.py $* || break; done
	@rm -fr *.pyc

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
