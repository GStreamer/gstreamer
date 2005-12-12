EXTRA_DIST = README

if BUILD_TESTS
## SUBDIRS_TESTS = tests testsuite
## FIXME: write tests from scratch
SUBDIRS_TESTS =
if HAVE_CHECK
SUBDIRS_CHECK = check
else
SUBDIRS_CHECK =
endif
else
SUBDIRS_TESTS =
SUBDIRS_CHECK =
endif

if BUILD_EXAMPLES
SUBDIRS_EXAMPLES = examples
else
SUBDIRS_EXAMPLES =
endif

SUBDIRS = \
	benchmarks	\
	$(SUBDIRS_CHECK) \
	misc		\
	$(SUBDIRS_TESTS) \
	$(SUBDIRS_EXAMPLES)

# These are all the possible subdirs
DIST_SUBDIRS = \
	benchmarks \
	check \
	examples \
	misc
