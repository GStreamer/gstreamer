TESTS_ENVIRONMENT=\
	GST_PLUGIN_PATH_ONLY=yes \
	GST_PLUGIN_PATH=$(top_builddir)/gst \
	GST_REGISTRY=$(top_builddir)/check/test-registry.xml

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

# rebuild gst-register-@GST_MAJORMINOR@ if needed
$(top_builddir)/tools/gst-register-@GST_MAJORMINOR@$(EXEEXT):
	cd $(top_builddir)/tools && make

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# ths core dumps of some machines have PIDs appended
CLEANFILES = core.* test-registry.xml

clean-local:
	for i in `find . -name ".libs" -type d`; do \
	  rm -rf $$i; \
	done

# FIXME: it'd be nicer to run the versioned register, but in that case
# "make dist" complains about not finding gst-register-0.9.c
TESTS = $(top_builddir)/tools/gst-register 	\
	gst/gst				     	\
	gst/gstbin			     	\
        gst/gstbuffer				\
        gst/gstbus				\
	gst/gstcaps     			\
	gst/gstghostpad				\
	gst/gstiterator 			\
	gst/gstmessage	 			\
	gst/gstobject				\
	gst/gstpad				\
	gst/gstsystemclock			\
	gst/gststructure			\
	gst/gsttag				\
	gst/gstvalue				\
	pipelines/simple_launch_lines		\
	pipelines/cleanup			\
	states/sinks				\
	gst-libs/gdp	

check_PROGRAMS = $(TESTS)

noinst_LTLIBRARIES = libgstcheck.la

libgstcheck_la_SOURCES = gstcheck.c
libgstcheck_la_LIBADD = $(GST_OBJ_LIBS)
noinst_HEADERS = gstcheck.h gst/capslist.h

AM_CFLAGS = $(GST_OBJ_CFLAGS) $(CHECK_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(CHECK_LIBS) libgstcheck.la

gst_libs_gdp_SOURCES = \
	gst-libs/gdp.c \
	$(top_srcdir)/libs/gst/dataprotocol/dataprotocol.c
# remove GST_ENABLE_NEW when dataprotocol has been declared API-stable
gst_libs_gdp_CFLAGS = $(AM_CFLAGS) -DGST_ENABLE_NEW
