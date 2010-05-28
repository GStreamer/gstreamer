SUBDIRS = common codegen gst examples plugin testsuite pkgconfig

common_cflags = $(PYTHON_INCLUDES) $(PYGOBJECT_CFLAGS) $(GST_CFLAGS) $(GST_OPTION_CFLAGS) -fno-strict-aliasing
common_libadd = $(GST_LIBS) $(GST_OPTION_LIBS)
common_ldflags = -module -avoid-version

# include before EXTRA_DIST for win32 assignment
include $(top_srcdir)/common/win32.mak

ACLOCAL_AMFLAGS = -I m4 -I common/m4

pyexec_LTLIBRARIES = 

if HAVE_PYGOBJECT_2_16
pyexec_LTLIBRARIES += gstoption.la
endif

# GStreamer option bindings
gstoption_la_CFLAGS = $(common_cflags)
gstoption_la_LIBADD = $(common_libadd)
gstoption_la_LDFLAGS = $(common_ldflags) \
	-export-symbols-regex "^(initgstoption|_PyGObject_API).*"
gstoption_la_SOURCES = gstoptionmodule.c

EXTRA_DIST = \
	gstlibtoolimporter.py \
	pygst.py.in \
	gst-python.spec.in \
	gst-python.spec \
	RELEASE gst-python.doap \
	$(win32)

BUILT_SOURCES = pygst.py
CLEANFILES = pygst.py pygst.pyc

include $(top_srcdir)/common/release.mak

pygst.py: $(top_srcdir)/pygst.py.in Makefile
	if test -f $@; then chmod +w $@; fi
	sed -e "s|@PYGSTDIR\@|$(shell pwd)|g" \
	    -e "s|@GST_MAJORMINOR\@|$(GST_MAJORMINOR)|g" \
	    $< > $@
	chmod -w $@

# rewrite pygst path in installed pygst.py, install pth file.
# they go in pyexecdir, should make 64bit users happy
install-exec-local: Makefile
	$(mkinstalldirs) $(DESTDIR)$(pyexecdir)
	sed -e "s|@PYGSTDIR\@|$(PYEXECDIR)/gst-$(GST_MAJORMINOR)|g" \
            -e "s|@GST_MAJORMINOR\@|$(GST_MAJORMINOR)|g" \
            $(srcdir)/pygst.py.in > $(DESTDIR)$(pyexecdir)/pygst.py
	chmod 644 $(DESTDIR)$(pyexecdir)/pygst.py
	echo "gst-$(GST_MAJORMINOR)" > $(DESTDIR)$(pyexecdir)/pygst.pth

uninstall-local:
	@rm $(DESTDIR)/$(pyexecdir)/pygst.py > /dev/null 2>&1 || true
	@rm $(DESTDIR)/$(pyexecdir)/pygst.pth > /dev/null 2>&1 || true

check-valgrind:
	cd testsuite/ && make check-valgrind

check-torture:
	@true

include $(top_srcdir)/common/coverage/lcov.mak

CRUFT_FILES = \
	$(top_builddir)/common/shave \
	$(top_builddir)/common/shave-libtool \
	$(top_builddir)/common/m4/libtool.m4 \
	$(top_builddir)/common/m4/ltoptions.m4 \
	$(top_builddir)/common/m4/ltsugar.m4 \
	$(top_builddir)/common/m4/ltversion.m4 \
	$(top_builddir)/common/m4/lt~obsolete.m4

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
