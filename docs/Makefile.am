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

SUBDIRS = gst libs $(PLUGIN_DOCS_DIRS)
DIST_SUBDIRS = gst libs plugins

EXTRA_DIST = version.entities.in list-ulink.xsl

upload:
	@if test "x$(SUBDIRS)" != x; then for a in $(SUBDIRS); do cd $$a; make upload; cd ..; done; fi

libs: gst

include $(top_srcdir)/common/parallel-subdirs.mak
