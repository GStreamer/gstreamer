if ENABLE_GTK_DOC
DOCS_SUBDIRS = libs
else
DOCS_SUBDIRS = 
endif

SUBDIRS = $(DOCS_SUBDIRS)
DIST_SUBDIRS = libs

EXTRA_DIST = \
	version.entities.in

upload:
	@if test "x$(SUBDIRS)" != x; then \
	  for a in $(SUBDIRS); do \
	    cd $$a; \
	    make upload; \
	    cd ..; \
	  done; \
	fi
