
plugin_LTLIBRARIES = libloadgst.la

tests_pass = dlopen_gst
tests_fail =
tests_ignore =

libloadgst_la_SOURCES = loadgst.c
libloadgst_la_CFLAGS = $(GST_CFLAGS)
libloadgst_la_LDFLAGS = -module -avoid-version $(GST_LIBS)

dlopen_gst_SOURCES = dlopen_gst.c
dlopen_gst_CFLAGS = $(GLIB_CFLAGS)
dlopen_gst_LDFLAGS = $(GLIB_LIBS)


# ../Rules is included because we specifically don't want the normal
# AM_CFLAGS, etc.

TESTS_ENVIRONMENT=\
	G_DEBUG=fatal_warnings \
	GST_PLUGIN_PATH=$(top_builddir)/gst:. \
	GST_REGISTRY=$(top_builddir)/testsuite/test-registry.xml

check_PROGRAMS = $(tests_pass) $(tests_fail) $(tests_ignore)

# make all tests depend on the versioned gst-register
$(tests_pass) $(tests_fail): $(top_builddir)/tools/gst-register-@GST_MAJORMINOR@

# rebuild gst-register-@GST_MAJORMINOR@ if needed
# the EXEEXT is because am 1.6 complained about overrides
$(top_builddir)/tools/gst-register-@GST_MAJORMINOR@$(EXEEXT):
	cd $(top_builddir)/tools && make

TESTS = $(top_builddir)/tools/gst-register-@GST_MAJORMINOR@ \
	$(tests_pass) $(tests_fail)
XFAIL_TESTS = $(tests_fail)

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

# This rule is here so make distcheck works on machines where core
# dumps have PIDs appended
CLEANFILES = core.*

