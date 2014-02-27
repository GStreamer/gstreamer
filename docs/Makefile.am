if ENABLE_GTK_DOC
if ENABLE_PLUGIN_DOCS
PLUGIN_DOCS_DIRS = plugins
else
PLUGIN_DOCS_DIRS =
endif
else
PLUGIN_DOCS_DIRS = plugins
endif

SUBDIRS = $(PLUGIN_DOCS_DIRS)
DIST_SUBDIRS = plugins

EXTRA_DIST = \
	version.entities.in

upload:
	@if test "x$(SUBDIRS)" != x; then for a in $(SUBDIRS); do cd $$a; make upload; cd ..; done; fi
