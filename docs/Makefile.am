if BUILD_DOCS
SUBDIRS_DOCS = libs
else
SUBDIRS_DOCS =
endif

SUBDIRS = $(SUBDIRS_DOCS)
DIST_SUBDIRS = libs

#upload:
#	@if test "x$(SUBDIRS_DOCS)" != x; then for a in $(SUBDIRS_DOCS); do cd $$a; make upload; cd ..; done; fi
