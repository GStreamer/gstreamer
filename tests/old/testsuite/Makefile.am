# FIXME : threads bytestream
SUBDIRS = caps cleanup plugin elements clock refcounting ## dynparams

GST_PLUGIN_PATH = $(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(GST_PLUGIN_PATH) GST_REGISTRY=`pwd`/test-registry.xml

testprogs = test_gst_init

TESTS = $(top_builddir)/tools/gst-register $(testprogs) gst-inspect-check

check_PROGRAMS = $(testprogs)

# we have nothing but apps here, we can do this safely
LIBS = $(GST_LIBS)
CFLAGS = $(GST_CFLAGS)

DIST_SUBDIRS = bytestream caps cleanup clock dynparams elements \
		plugin refcounting threads

EXTRA_DIST = gst-inspect-check
