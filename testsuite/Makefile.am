if GST_DISABLE_PARSE
GST_PARSE_DIRS = 
else
GST_PARSE_DIRS = parse
endif

if GST_DISABLE_GST_DEBUG
GST_DEBUG_DIRS = 
else
GST_DEBUG_DIRS = debug
endif

# FIXME : threads bytestream
SUBDIRS = caps plugin elements clock refcounting threads indexers debug $(GST_PARSE_DIRS) $(GST_DEBUG_DIRS) ## cleanup dynparams

TOP_BUILDDIR=$(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(TOP_BUILDDIR) GST_REGISTRY=$(TOP_BUILDDIR)/testsuite/test-registry.xml

if BUILD_FAILING_TESTS
testprogs = test_gst_init
inspectcheck = gst-inspect-check
else
testprogs =
inspectcheck = 
endif

# since these TESTS are only run after recursing into subdirs,
# gst-register should be run before that, in the first subdir that uses it
# TESTS = $(top_builddir)/tools/gst-register $(testprogs) $(inspectcheck)
TESTS = $(testprogs) $(inspectcheck)

check_PROGRAMS = $(testprogs)

# we have nothing but apps here, we can do this safely
LIBS = $(GST_LIBS)
AM_CFLAGS = $(GST_CFLAGS)

# we makes them, we gots to clean them
CLEANFILES = test-registry.xml

DIST_SUBDIRS = bytestream caps cleanup clock dynparams elements indexers \
		plugin refcounting threads parse debug

EXTRA_DIST = gst-inspect-check
