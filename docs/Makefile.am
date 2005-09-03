if ENABLE_GTK_DOC
SUBDIRS_GTK_DOC = gst libs
else
SUBDIRS_GTK_DOC =
endif

if ENABLE_DOCBOOK
SUBDIRS_DOCBOOK = faq manual pwg
else
SUBDIRS_DOCBOOK =
endif

SUBDIRS = $(SUBDIRS_DOCBOOK) $(SUBDIRS_GTK_DOC)
DIST_SUBDIRS = gst faq manual pwg libs xsl

EXTRA_DIST = \
	slides manuals.mak htmlinstall.mak upload.mak \
	image-png image-pdf image-eps url.entities version.entities.in

upload:
	@if test "x$(SUBDIRS)" != x; then for a in $(SUBDIRS); do cd $$a; make upload; cd ..; done; fi
