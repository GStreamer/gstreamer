if BUILD_FAILING_TESTS
FAILING = cleanup3 cleanup4
else
FAILING =
endif

testprogs = cleanup1 cleanup2 $(FAILING) cleanup5

GST_PLUGIN_PATH = $(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(GST_PLUGIN_PATH) GST_REGISTRY=`pwd`/test-registry.xml

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

# we have nothing but apps here, we can do this safely
cleanup1_LDADD = $(GST_LIBS) #-ldmallocth
cleanup1_CFLAGS = $(GST_CFLAGS) $(XML_CFLAGS) $(GLIB_CFLAGS)
cleanup2_LDADD = $(GST_LIBS) 
cleanup2_CFLAGS = $(GST_CFLAGS) $(XML_CFLAGS) $(GLIB_CFLAGS)
cleanup3_LDADD = $(GST_LIBS) 
cleanup3_CFLAGS = $(GST_CFLAGS) $(XML_CFLAGS) $(GLIB_CFLAGS)
cleanup4_LDADD = $(GST_LIBS) 
cleanup4_CFLAGS = $(GST_CFLAGS) $(XML_CFLAGS) $(GLIB_CFLAGS)
cleanup5_LDADD = $(GST_LIBS) 
cleanup5_CFLAGS = $(GST_CFLAGS) $(XML_CFLAGS) $(GLIB_CFLAGS)
