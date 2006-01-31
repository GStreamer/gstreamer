if ENABLE_GTK_DOC
GTK_DOC_DIRS = plugins
else
GTK_DOC_DIRS =
endif

SUBDIRS = $(GTK_DOC_DIRS)
DIST_SUBDIRS = plugins

EXTRA_DIST = \
        upload.mak \
        version.entities.in

upload:
	@if test "x$(SUBDIRS)" != x; then for a in $(SUBDIRS); do cd $$a; make upload; cd ..; done; fi
