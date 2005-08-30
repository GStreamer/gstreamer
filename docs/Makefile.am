if ENABLE_GTK_DOC
GTK_DOC_DIRS = plugins
else
GTK_DOC_DIRS =
endif

SUBDIRS = $(GTK_DOC_DIRS)
DIST_SUBDIRS = plugins

EXTRA_DIST = \
        random/ChangeLog-0.8
upload.mak \
        version.entities.in

#upload:
#	@if test "x$(SUBDIRS_DOCS)" != x; then for a in $(SUBDIRS_DOCS); do cd $$a; make upload; cd ..; done; fi
