TOP_BUILDDIR=$(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(TOP_BUILDDIR) GST_REGISTRY=$(TOP_BUILDDIR)/testsuite/test-registry.xml

testprogs = clock1 clock2

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

LDADD = $(GST_LIBS) 
AM_CFLAGS = $(GST_CFLAGS)

