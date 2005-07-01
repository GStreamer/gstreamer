if BUILD_DOCS
SUBDIRS_DOCS = docs
else
SUBDIRS_DOCS =
endif

UNCONDDIRS = examples gst pkgconfig testsuite
SUBDIRS = \
	$(UNCONDDIRS) \
	$(SUBDIRS_DOCS)
DIST_SUBDIRS = $(UNCONDDIRS) docs

EXTRA_DIST = \
	ltihooks.py \
	gst-python.spec.in \
	gst-python.spec \
	README-docs \
	RELEASE

include $(top_srcdir)/common/release.mak

# install pth file.
install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(pythondir)
	echo "gst-$(GST_MAJORMINOR)" > $(DESTDIR)$(pyexecdir)/pygst.pth
install-exec-local:
	$(mkinstalldirs) $(DESTDIR)$(pyexecdir)
	echo "gst-$(GST_MAJORMINOR)" > $(DESTDIR)$(pyexecdir)/pygst.pth
uninstall-local:
	rm -f $(DESTDIR)$(pythondir)/pygst.pth $(DESTDIR)$(pyexecdir)/pygst.pth

# this file is common to all pygtk versions.
pyexec_PYTHON = pygst.py
