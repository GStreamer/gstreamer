if ENABLE_PLUGIN_DOCS
PLUGIN_DOCS_DIRS = plugins
else
PLUGIN_DOCS_DIRS =
endif

if ENABLE_GTK_DOC
GTK_DOC_DIRS = $(PLUGINS_DOCS_DIRS)
else
GTK_DOC_DIRS =
endif

SUBDIRS = $(GTK_DOC_DIRS)
DIST_SUBDIRS = plugins

EXTRA_DIST = \
        random/ChangeLog-0.8 \
        version.entities.in
