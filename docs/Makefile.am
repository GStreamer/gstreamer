if ENABLE_GTK_DOC
SUBDIRS_GTK_DOC = gst libs plugins
else
SUBDIRS_GTK_DOC =
endif

if ENABLE_DOCBOOK
SUBDIRS_DOCBOOK = faq manual pwg
else
SUBDIRS_DOCBOOK =
endif

BUILT_SOURCES = version.entities

SUBDIRS = $(SUBDIRS_DOCBOOK) $(SUBDIRS_GTK_DOC)
DIST_SUBDIRS = gst libs plugins faq manual pwg xsl

EXTRA_DIST = \
	slides manuals.mak htmlinstall.mak upload.mak \
	image-png image-pdf image-eps url.entities version.entities.in

upload:
	@if test "x$(SUBDIRS)" != x; then for a in $(SUBDIRS); do cd $$a; make upload; cd ..; done; fi
