# Don't try to use wildcards to replace the list of tests below.
# http://www.gnu.org/software/automake/manual/automake.html#Wildcards
# Keep this list sorted!

TEST_ENVIRONMENT = \
	GST_OVERRIDE_SRC_PATH="$(abs_top_srcdir)/gi/overrides" \
	GST_OVERRIDE_BUILD_PATH="$(abs_top_builddir)/gi/overrides"

tests =	\
	test_gst.py \
	test_types.py

EXTRA_DIST = \
	__init__.py \
	cleanup.py \
	common.py \
	gstpython.supp \
	meson.build \
	overrides_hack.py \
	python.supp \
	runtests.py \
	$(tests)

clean-local:
	rm -rf *.pyc *.pyo

check-local:
	$(TEST_ENVIRONMENT) $(PYTHON) $(srcdir)/runtests.py $(tests)

%.check: %
	$(TEST_ENVIRONMENT) $(PYTHON) $(srcdir)/runtests.py $*
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
