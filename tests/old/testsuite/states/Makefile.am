GST_PLUGIN_PATH=$(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(GST_PLUGIN_PATH) GST_REGISTRY=$(GST_PLUGIN_PATH)/testsuite/test-registry.xml

testprogs = locked

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

locked_LDADD = $(GST_LIBS) 
locked_CFLAGS = $(GST_CFLAGS) $(GNOME_CFLAGS) $(XML_CFLAGS)

