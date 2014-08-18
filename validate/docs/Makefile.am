SUBDIRS = validate
DIST_SUBDIRS = validate

upload:
	@if test "x$(SUBDIRS)" != x; then for a in $(SUBDIRS); do cd $$a; make upload; cd ..; done; fi

libs: validate

include $(top_srcdir)/common/parallel-subdirs.mak
