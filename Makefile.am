if BUILD_DOCS
SUBDIRS_DOCS = docs
else
SUBDIRS_DOCS =
endif

UNCONDDIRS = gst pkgconfig testsuite
SUBDIRS = \
	$(UNCONDDIRS) \
	$(SUBDIRS_DOCS)
DIST_SUBDIRS = $(UNCONDDIRS) docs

EXTRA_DIST = \
	gst-python.spec.in \
	gst-python.spec \
	README-docs \
	examples/gst/bps.py \
	examples/gst/cp.py \
	examples/gst/dvdplay.py \
	examples/gst/f2f.py \
	examples/gst/identity.py \
	examples/gst/ilat.py \
	examples/gst/lat.py \
	examples/gst/rot13.py \
	examples/gst/vorbisplay.py \
	examples/gst/wavenc.py \
	examples/gstplay/player.py \
        codegen/__init__.py \
        codegen/argtypes.py \
        codegen/codegen.py \
        codegen/definitions.py \
        codegen/defsparser.py \
        codegen/docextract.py \
        codegen/docgen.py \
        codegen/h2def.py \
        codegen/mergedefs.py \
        codegen/mkskel.py \
        codegen/override.py \
        codegen/scmexpr.py

include $(top_srcdir)/common/release.mak
