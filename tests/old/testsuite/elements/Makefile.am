GST_PLUGIN_PATH=$(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(GST_PLUGIN_PATH) GST_REGISTRY=$(GST_PLUGIN_PATH)/testsuite/test-registry.xml

testprogs = tee fake name

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

EXTRA_DIST = property.h
LDADD = $(GST_LIBS) 
CFLAGS = $(GST_CFLAGS)

