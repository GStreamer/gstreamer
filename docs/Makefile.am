if ENABLE_DOCBOOK
SUBDIRS_DOCBOOK = faq manual pwg
else
SUBDIRS_DOCBOOK =
endif

if ENABLE_GTK_DOC
if ENABLE_PLUGIN_DOCS
PLUGIN_DOCS_DIRS = plugins
else
PLUGIN_DOCS_DIRS =
endif
else
PLUGIN_DOCS_DIRS = plugins
endif

BUILT_SOURCES = version.entities

SUBDIRS = design gst libs $(PLUGIN_DOCS_DIRS) $(SUBDIRS_DOCBOOK)
DIST_SUBDIRS = design gst libs plugins faq manual pwg slides xsl

EXTRA_DIST = \
	manuals.mak htmlinstall.mak \
	image-png image-pdf image-eps url.entities version.entities.in

upload:
	@if test "x$(SUBDIRS)" != x; then for a in $(SUBDIRS); do cd $$a; make upload; cd ..; done; fi

