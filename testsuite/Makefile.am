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
	buffer.py \
        caps.py \
	common.py \
	element.py \
	test_event.py \
        interface.py \
	pad.py \
        pipeline.py \
	test_xml.py

check-local: testhelper.la
	@PYTHONPATH=$(PYTHONPATH):$(top_builddir):$(top_builddir)/gst/.libs $(PYTHON) $(srcdir)/runtests.py
	@rm -fr *.pyc

EXTRA_DIST = $(tests) runtests.py test-object.h

