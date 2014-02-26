if ENABLE_GTK_DOC
if ENABLE_PLUGIN_DOCS
PLUGIN_DOCS_DIRS = plugins
else
PLUGIN_DOCS_DIRS =
endif
else
PLUGIN_DOCS_DIRS = plugins
endif

SUBDIRS = libs $(PLUGIN_DOCS_DIRS)
DIST_SUBDIRS = libs plugins

EXTRA_DIST = \
        random/ChangeLog-0.8 \
        version.entities.in

upload:
	@if test "x$(SUBDIRS)" != x; then for a in $(SUBDIRS); do cd $$a; make upload; cd ..; done; fi
