if ENABLE_GTK_DOC
GTK_DOC_DIRS = libs
else
GTK_DOC_DIRS =
endif

if ENABLE_PLUGIN_DOCS
PLUGIN_DOCS_DIRS = plugins
else
PLUGIN_DOCS_DIRS =
endif

SUBDIRS = design $(GTK_DOC_DIRS) $(PLUGIN_DOCS_DIRS)
DIST_SUBDIRS = design libs plugins

EXTRA_DIST = \
	design-audiosinks.txt \
        version.entities.in

upload:
	@if test "x$(SUBDIRS)" != x; then for a in $(SUBDIRS); do cd $$a; make upload; cd ..; done; fi
