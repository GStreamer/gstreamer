GST_PLUGIN_PATH=$(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(GST_PLUGIN_PATH) GST_REGISTRY=$(GST_PLUGIN_PATH)/testsuite/test-registry.xml

testprogs = clock1

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

LDADD = $(GST_LIBS) 
CFLAGS = $(GST_CFLAGS)

