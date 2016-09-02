EXTRA_DIST = README

# Build misc unless both --disable-examples and --disable-benchmarks were passed

if BUILD_BENCHMARKS
SUBDIRS_BENCHMARKS = benchmarks
SUBDIRS_MISC = misc
else
SUBDIRS_BENCHMARKS =
endif

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
SUBDIRS_MISC = misc
else
SUBDIRS_EXAMPLES =
endif

SUBDIRS = \
	$(SUBDIRS_BENCHMARKS) \
	$(SUBDIRS_CHECK) \
	$(SUBDIRS_MISC) \
	$(SUBDIRS_TESTS) \
	$(SUBDIRS_EXAMPLES)

# These are all the possible subdirs
DIST_SUBDIRS = \
	benchmarks \
	check \
	examples \
	misc
