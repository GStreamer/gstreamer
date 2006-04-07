SUBDIRS = codegen gst examples testsuite pkgconfig

EXTRA_DIST = \
	ltihooks.py \
	pygst.py.in \
	gst-python.spec.in \
	gst-python.spec \
	RELEASE

BUILT_SOURCES = pygst.py
CLEANFILES = pygst.py

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
