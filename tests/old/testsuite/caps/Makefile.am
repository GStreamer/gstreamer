GST_PLUGIN_PATH=$(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(GST_PLUGIN_PATH) GST_REGISTRY=$(GST_PLUGIN_PATH)/testsuite/test-registry.xml

testprogs = intersection compatibility normalisation

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

# we have nothing but apps here, we can do this safely
intersection_LDADD = $(GST_LIBS) 
intersection_CFLAGS = $(GST_CFLAGS) $(GNOME_CFLAGS) $(XML_CFLAGS)
compatibility_LDADD = $(GST_LIBS) 
compatibility_CFLAGS = $(GST_CFLAGS) $(GNOME_CFLAGS) $(XML_CFLAGS)
normalisation_LDADD = $(GST_LIBS) 
normalisation_CFLAGS = $(GST_CFLAGS) $(GNOME_CFLAGS) $(XML_CFLAGS)

