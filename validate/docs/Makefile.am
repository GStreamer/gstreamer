SUBDIRS = validate plugins
DIST_SUBDIRS = validate launcher plugins

if HAVE_SPHINHX
SUBDIRS += launcher
endif

upload:
	@if test "x$(SUBDIRS)" != x; then for a in $(SUBDIRS); do cd $$a; make upload; cd ..; done; fi

libs: validate

include $(top_srcdir)/common/parallel-subdirs.mak
