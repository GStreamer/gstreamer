# FIXME : threads bytestream
SUBDIRS = caps cleanup plugin elements clock refcounting ## dynparams

GST_PLUGIN_PATH = $(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(GST_PLUGIN_PATH) GST_REGISTRY=`pwd`/test-registry.xml

if BUILD_FAILING_TESTS
testprogs = test_gst_init
inspectcheck = gst-inspect-check
else
testprogs =
inspectcheck = 
endif

TESTS = $(top_builddir)/tools/gst-register $(testprogs) $(inspectcheck)

check_PROGRAMS = $(testprogs)

# we have nothing but apps here, we can do this safely
LIBS = $(GST_LIBS)
CFLAGS = $(GST_CFLAGS)

DIST_SUBDIRS = bytestream caps cleanup clock dynparams elements \
		plugin refcounting threads

EXTRA_DIST = gst-inspect-check
