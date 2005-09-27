INCLUDES = \
	$(PYTHON_INCLUDES) \
	$(PYGTK_CFLAGS) \
	$(PYGST_CFLAGS) \
	$(GST_CFLAGS)

noinst_LTLIBRARIES = testhelper.la
linked_LIBS = testhelper.la

testhelper_la_LDFLAGS = -module -avoid-version
testhelper_la_LIBADD = $(GLIB_LIBS)
testhelper_la_SOURCES = \
	testhelpermodule.c \
	test-object.c

# This is a hack to make sure a shared library is built
testhelper.la: $(testhelper_la_OBJECTS) $(testhelper_la_DEPENDENCIES)
	$(LINK) -rpath $(pkgpyexecdir) $(testhelper_la_LDFLAGS) $(testhelper_la_OBJECTS) $(testhelper_la_LIBADD) $(LIBS)

tests =  \
	test_bin.py \
	test_buffer.py \
        test_caps.py \
	test_element.py \
	test_event.py \
        test_interface.py \
	test_pad.py \
        test_pipeline.py \
        test_registry.py \
        test_struct.py \
	test_xml.py

check-local: testhelper.la
	@G_DEBUG=fatal_warnings PYTHONPATH=$(top_builddir):$(top_builddir)/gst/.libs:`pwd`:$(top_srcdir):$(PYTHONPATH) $(PYTHON) $(srcdir)/runtests.py
	@rm -fr *.pyc

check-verbose: testhelper.la
	@VERBOSE=yes G_DEBUG=fatal_warnings PYTHONPATH=$(top_builddir):$(top_builddir)/gst/.libs:`pwd`:$(top_srcdir):$(PYTHONPATH) $(PYTHON) $(srcdir)/runtests.py
	@rm -fr *.pyc

EXTRA_DIST = $(tests) common.py runtests.py test-object.h

