if BUILD_DOCS
SUBDIRS_DOCS = docs
else
SUBDIRS_DOCS =
endif

UNCONDDIRS = gst pkgconfig testsuite
SUBDIRS = \
	$(UNCONDDIRS) \
	$(SUBDIRS_DOCS)
DIST_SUBDIRS = $(UNCONDDIRS) docs $(PLAYDIR)

EXTRA_DIST = \
	gst-python.spec.in \
	gst-python.spec \
	README-docs
	examples/gst/bps.py \
	examples/gst/cp.py \
	examples/gst/dvdplay.py \
	examples/gst/f2f.py \
	examples/gst/identity.py \
	examples/gst/ilat.py \
	examples/gst/lat.py \
	examples/gst/rot13.py \
	examples/gst/vorbisplay.py \
	examples/gstplay/player.py

snap:
	$(MAKE) dist distdir=$(PACKAGE)-`date +"%Y%m%d"`
