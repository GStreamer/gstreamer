# This is an include file specifically tuned for building documentation
# for GStreamer plug-ins

help:
	@echo "If you are a doc maintainer, run 'make update' to update"
	@echo "the documentation files maintained in CVS"

# update the stuff maintained by doc maintainers
update:
	make inspect-update
	make scanobj-update

# We set GPATH here; this gives us semantics for GNU make
# which are more like other make's VPATH, when it comes to
# whether a source that is a target of one rule is then
# searched for in VPATH/GPATH.
#
GPATH = $(srcdir)

# thomas: make docs parallel installable
TARGET_DIR=$(HTML_DIR)/$(DOC_MODULE)-@GST_MAJORMINOR@

EXTRA_DIST = 				\
	scanobj-build.stamp		\
	$(srcdir)/inspect/*.xml		\
	inspect.stamp			\
	inspect-build.stamp		\
	$(SCANOBJ_FILES)		\
	$(content_files)		\
	$(extra_files)			\
	$(HTML_IMAGES)			\
	$(DOC_MAIN_SGML_FILE)	\
	$(DOC_MODULE).types		\
	$(DOC_OVERRIDES)		\
	$(DOC_MODULE)-sections.txt

MAINTAINER_DOC_STAMPS =			\
	scanobj-build.stamp		\
	inspect-build.stamp		\
	inspect.stamp

# we don't add inspect-build.stamp and scanobj-build.stamp here since they are
# built manually by docs maintainers and result is commited to CVS
DOC_STAMPS =				\
	scan-build.stamp		\
	tmpl-build.stamp		\
	sgml-build.stamp		\
	html-build.stamp		\
	scan.stamp			\
	tmpl.stamp			\
	sgml.stamp			\
	html.stamp

# files generated/updated by gtkdoc-scangobj
SCANOBJ_FILES =				\
	$(DOC_MODULE).signals           \
        $(DOC_MODULE).hierarchy         \
        $(DOC_MODULE).interfaces        \
        $(DOC_MODULE).prerequisites     \
        $(DOC_MODULE).args

SCANOBJ_FILES_O =			\
	.libs/$(DOC_MODULE)-scan.o

# files generated/updated by gtkdoc-scan
SCAN_FILES =				\
	$(DOC_MODULE)-sections.txt	\
	$(DOC_MODULE)-overrides.txt	\
	$(DOC_MODULE)-undocumented.txt	\
	$(DOC_MODULE)-decl.txt		\
	$(DOC_MODULE)-decl-list.txt

if ENABLE_GTK_DOC
all-local: html-build.stamp

#### scan gobjects; done by documentation maintainer ####
scanobj-update:
	-rm scanobj-build.stamp
	make scanobj-build.stamp

# in the case of non-srcdir builds, the built gst directory gets added
# to gtk-doc scanning; but only then, to avoid duplicates
# FIXME: since we don't have the scan step as part of the build anymore,
# we could remove that
# TODO: finish elite script that updates the output files of this step
# instead of rewriting them, so that multiple maintainers can generate
# a collective set of args and signals
scanobj-build.stamp: $(SCANOBJ_DEPS) $(basefiles)
	@echo '*** Scanning GObjects ***'
	if test x"$(srcdir)" != x. ; then				\
	    for f in $(SCANOBJ_FILES);					\
	    do								\
	        cp $(srcdir)/$$f . ;					\
	    done;							\
	else								\
	    $(INSPECT_ENVIRONMENT) 					\
	    CC="$(GTKDOC_CC)" LD="$(GTKDOC_LD)" 			\
	    CFLAGS="-g $(GTKDOC_CFLAGS)" LDFLAGS="$(GTKDOC_LIBS)"		\
	    $(GST_DOC_SCANOBJ) --type-init-func="gst_init(NULL,NULL)"	\
	        --module=$(DOC_MODULE) --source=$(PACKAGE) &&		\
		$(PYTHON)						\
		$(top_srcdir)/common/scangobj-merge.py $(DOC_MODULE);	\
	fi
	touch scanobj-build.stamp

$(DOC_MODULE)-decl.txt $(SCANOBJ_FILES) $(SCANOBJ_FILES_O): scan-build.stamp
	@true

### inspect GStreamer plug-ins; done by documentation maintainer ###

# only look at the plugins in this module when building inspect .xml stuff
INSPECT_REGISTRY=$(top_builddir)/docs/plugins/inspect-registry.xml
INSPECT_ENVIRONMENT=\
        GST_PLUGIN_SYSTEM_PATH= \
        GST_PLUGIN_PATH=$(top_builddir)/gst:$(top_builddir)/sys:$(top_builddir)/ext:$(top_builddir)/plugins:$(top_builddir)/src \
        GST_REGISTRY=$(INSPECT_REGISTRY)

# update the element and plugin XML descriptions; store in inspect/
inspect:
	mkdir inspect

inspect-update: inspect
	-rm $(INSPECT_REGISTRY)
	-rm inspect-build.stamp
	make inspect-build.stamp

# FIXME: inspect.stamp should be written to by gst-xmlinspect.py
# IFF the output changed; see gtkdoc-mktmpl
inspect-build.stamp:
	@echo '*** Rebuilding plugin inspection files ***'
	if test x"$(srcdir)" != x. ; then \
	    cp $(srcdir)/inspect.stamp . ; \
	    cp $(srcdir)/inspect-build.stamp . ; \
	else \
	    $(INSPECT_ENVIRONMENT) $(PYTHON) \
	        $(top_srcdir)/common/gst-xmlinspect.py $(PACKAGE) inspect && \
	    echo -n "timestamp" > inspect.stamp && \
	    touch inspect-build.stamp; \
        fi

### scan headers; done on every build ###
scan-build.stamp: $(HFILE_GLOB) $(EXTRA_HFILES) $(basefiles) scanobj-build.stamp inspect-build.stamp
	if test "x$(top_srcdir)" != "x$(top_builddir)" &&		\
	   test -d "$(top_builddir)/gst";				\
        then								\
            export BUILT_OPTIONS="--source-dir=$(top_builddir)/gst";	\
        fi;								\
	gtkdoc-scan							\
	    $(SCAN_OPTIONS) $(EXTRA_HFILES)				\
	    --module=$(DOC_MODULE)					\
	    $$BUILT_OPTIONS						\
	    --ignore-headers="$(IGNORE_HFILES)";			\
	touch scan-build.stamp

#### update templates; done on every build ####

### FIXME: make this error out again when docs are fixed for 0.9
# in a non-srcdir build, we need to copy files from the previous step
# and the files from previous runs of this step
tmpl-build.stamp: $(DOC_MODULE)-decl.txt $(SCANOBJ_FILES) $(DOC_MODULE)-sections.txt $(DOC_OVERRIDES)
	@echo '*** Rebuilding template files ***'
	if test x"$(srcdir)" != x. ; then				\
	    for f in $(SCANOBJ_FILES) $(SCAN_FILES);			\
	    do								\
	        if test -e $(srcdir)/$$f; then cp $(srcdir)/$$f . ; fi; \
	    done;							\
	fi
	gtkdoc-mktmpl --module=$(DOC_MODULE) | tee tmpl-build.log
	$(PYTHON) \
		$(top_srcdir)/common/mangle-tmpl.py $(srcdir)/inspect tmpl
	@cat $(DOC_MODULE)-unused.txt
	rm -f tmpl-build.log
	touch tmpl-build.stamp

tmpl.stamp: tmpl-build.stamp
	@true

#### build xml; done on every build ####

### FIXME: make this error out again when docs are fixed for 0.9
sgml-build.stamp: tmpl.stamp inspect.stamp $(CFILE_GLOB) $(top_srcdir)/common/plugins.xsl
	@echo '*** Building XML ***'
	@-mkdir -p xml
	@for a in $(srcdir)/inspect/*.xml; do \
            xsltproc --stringparam module $(MODULE) \
		$(top_srcdir)/common/plugins.xsl $$a > xml/`basename $$a`; done
	@for f in $(EXAMPLE_CFILES); do \
		$(PYTHON) $(top_srcdir)/common/c-to-xml.py $$f > xml/element-`basename $$f .c`.xml; done
	gtkdoc-mkdb \
		--module=$(DOC_MODULE) \
		--source-dir=$(DOC_SOURCE_DIR) \
		--main-sgml-file=$(srcdir)/$(DOC_MAIN_SGML_FILE) \
		--output-format=xml \
		--ignore-files="$(IGNORE_HFILES) $(IGNORE_CFILES)" \
		$(MKDB_OPTIONS) \
		| tee sgml-build.log
	@if grep "WARNING:" sgml-build.log > /dev/null; then true; fi # exit 1; fi
	cp ../version.entities xml
	rm sgml-build.log
	touch sgml-build.stamp

sgml.stamp: sgml-build.stamp
	@true

#### build html; done on every step ####

html-build.stamp: sgml.stamp $(DOC_MAIN_SGML_FILE) $(content_files)
	@echo '*** Building HTML ***'
	if test -d html; then rm -rf html; fi
	mkdir html
	cp $(srcdir)/$(DOC_MAIN_SGML_FILE) html
	@for f in $(content_files); do cp $(srcdir)/$$f html; done
	cp -pr xml html
	cp ../version.entities html
	cd html && gtkdoc-mkhtml $(DOC_MODULE) $(DOC_MAIN_SGML_FILE) \
	    2>&1 | tee ../html-build.log
	@if grep "warning:" html-build.log > /dev/null; then \
		echo "ERROR"; grep "warning:" html-build.log; exit 1; fi
	@rm html-build.log
	rm -f html/$(DOC_MAIN_SGML_FILE)
	rm -rf html/xml
	rm -f html/version.entities
	test "x$(HTML_IMAGES)" = "x" || for i in "" $(HTML_IMAGES) ; do \
	    if test "$$i" != ""; then cp $(srcdir)/$$i html ; fi; done
	@echo '-- Fixing Crossreferences' 
	gtkdoc-fixxref --module-dir=html --html-dir=$(HTML_DIR) $(FIXXREF_OPTIONS)
	touch html-build.stamp
else
all-local:
endif

# FC3 seems to need -scan.c to be part of CLEANFILES for distcheck
# no idea why FC4 can do without
CLEANFILES = \
	$(SCANOBJ_FILES_O) \
	$(DOC_MODULE)-scan.c \
	$(DOC_MODULE)-unused.txt \
	$(DOC_STAMPS) \
	inspect-registry.xml

# FIXME: these rules need a little cleaning up
clean-local:
	rm -f *~ *.bak
	rm -rf .libs
# clean files generated for tmpl build
	-rm -rf tmpl
# clean files copied/generated for nonsrcdir tmpl build
	if test x"$(srcdir)" != x. ; then \
	    rm -rf $(SCANOBJ_FILES) $(SCAN_FILES);			\
	fi
# clean files generated for xml build
	-rm -rf xml
# clean files generate for html build
	-rm -rf html

distclean-local: clean
	rm -rf tmpl/*.sgml.bak
	rm -f *.stamp || true
	rm -rf *.o

# thomas: make docs parallel installable; devhelp requires majorminor too
install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(TARGET_DIR) 
	(installfiles=`echo ./html/*.html`; \
	if test "$$installfiles" = './html/*.html'; \
	then echo '-- Nothing to install' ; \
	else \
	  for i in $$installfiles; do \
	    echo '-- Installing '$$i ; \
	    $(INSTALL_DATA) $$i $(DESTDIR)$(TARGET_DIR); \
	  done; \
	  pngfiles=`echo ./html/*.png`; \
	  if test "$$pngfiles" != './html/*.png'; then \
	    for i in $$pngfiles; do \
	      echo '-- Installing '$$i ; \
	      $(INSTALL_DATA) $$i $(DESTDIR)$(TARGET_DIR); \
	    done; \
	  fi; \
	  echo '-- Installing $(srcdir)/html/$(DOC_MODULE).devhelp' ; \
	  $(INSTALL_DATA) $(srcdir)/html/$(DOC_MODULE).devhelp \
	    $(DESTDIR)$(TARGET_DIR)/$(DOC_MODULE)-@GST_MAJORMINOR@.devhelp; \
	  echo '-- Installing $(srcdir)/html/index.sgml' ; \
	  $(INSTALL_DATA) $(srcdir)/html/index.sgml $(DESTDIR)$(TARGET_DIR); \
		if test -e $(srcdir)/html/style.css; then \
			echo '-- Installing $(srcdir)/html/style.css' ; \
			$(INSTALL_DATA) $(srcdir)/html/style.css $(DESTDIR)$(TARGET_DIR); \
		fi; \
	fi) 
uninstall-local:
	(installfiles=`echo ./html/*.html`; \
	if test "$$installfiles" = './html/*.html'; \
	then echo '-- Nothing to uninstall' ; \
	else \
	  for i in $$installfiles; do \
	    rmfile=`basename $$i` ; \
	    echo '-- Uninstalling $(DESTDIR)$(TARGET_DIR)/'$$rmfile ; \
	    rm -f $(DESTDIR)$(TARGET_DIR)/$$rmfile; \
	  done; \
	  pngfiles=`echo ./html/*.png`; \
	  if test "$$pngfiles" != './html/*.png'; then \
	    for i in $$pngfiles; do \
	      rmfile=`basename $$i` ; \
	      echo '-- Uninstalling $(DESTDIR)$(TARGET_DIR)/'$$rmfile ; \
	      rm -f $(DESTDIR)$(TARGET_DIR)/$$rmfile; \
	    done; \
	  fi; \
	  echo '-- Uninstalling $(DESTDIR)$(TARGET_DIR)/$(DOC_MODULE).devhelp' ; \
	  rm -f $(DESTDIR)$(TARGET_DIR)/$(DOC_MODULE)-@GST_MAJORMINOR@.devhelp; \
	  echo '-- Uninstalling $(DESTDIR)$(TARGET_DIR)/index.sgml' ; \
	  rm -f $(DESTDIR)$(TARGET_DIR)/index.sgml; \
		if test -e $(DESTDIR)$(TARGET_DIR)/style.css; then \
			echo '-- Uninstalling $(DESTDIR)$(TARGET_DIR)/style.css' ; \
			rm -f $(DESTDIR)$(TARGET_DIR)/style.css; \
		fi; \
	fi) 
	if test -d $(DESTDIR)$(TARGET_DIR); then rmdir -p --ignore-fail-on-non-empty $(DESTDIR)$(TARGET_DIR) 2>/dev/null; fi; true

#
# Checks
#
check-hierarchy: $(DOC_MODULE).hierarchy
	@if grep '	' $(DOC_MODULE).hierarchy; then \
	    echo "$(DOC_MODULE).hierarchy contains tabs, please fix"; \
	    /bin/false; \
	fi

check: check-hierarchy


#
# Require gtk-doc when making dist
#
if ENABLE_GTK_DOC
dist-check-gtkdoc:
else
dist-check-gtkdoc:
	@echo "*** gtk-doc must be installed and enabled in order to make dist"
	@false
endif

# FIXME: decide whether we want to dist generated html or not
dist-hook: dist-check-gtkdoc dist-hook-local
	mkdir $(distdir)/tmpl
	mkdir $(distdir)/xml
	mkdir $(distdir)/html
	-cp $(srcdir)/tmpl/*.sgml $(distdir)/tmpl
	-cp $(srcdir)/sgml/*.xml $(distdir)/xml
	-cp $(srcdir)/html/index.sgml $(distdir)/html
	-cp $(srcdir)/html/*.html $(srcdir)/html/*.css $(distdir)/html
	-cp $(srcdir)/html/$(DOC_MODULE).devhelp $(distdir)/html

	images=$(HTML_IMAGES) ;    	      \
	for i in "" $$images ; do		      \
	  if test "$$i" != ""; then cp $(srcdir)/$$i $(distdir)/html ; fi; \
	done

.PHONY : dist-hook-local

