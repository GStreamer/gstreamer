TOP_BUILDDIR=$(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(TOP_BUILDDIR) GST_REGISTRY=$(TOP_BUILDDIR)/testsuite/test-registry.xml

testprogs = intersection compatibility normalisation string-conversions

# we run gst-register here, which is a HACK to generate the test registry
# before we actually run the real tests
# a better fix is welcome :)
TESTS = $(top_builddir)/tools/gst-register $(testprogs)

check_PROGRAMS = $(testprogs)

intersection_LDADD = $(GST_LIBS) 
intersection_CFLAGS = $(GST_CFLAGS) $(GNOME_CFLAGS) $(XML_CFLAGS)
compatibility_LDADD = $(GST_LIBS) 
compatibility_CFLAGS = $(GST_CFLAGS) $(GNOME_CFLAGS) $(XML_CFLAGS)
normalisation_LDADD = $(GST_LIBS) 
normalisation_CFLAGS = $(GST_CFLAGS) $(GNOME_CFLAGS) $(XML_CFLAGS)
string_conversions_LDADD = $(GST_LIBS) 
string_conversions_CFLAGS = $(GST_CFLAGS) $(GNOME_CFLAGS) $(XML_CFLAGS)
