## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
# DOC_MODULE=gstreamer-@GST_MAJORMINOR@
DOC_MODULE=gstreamer

# for upload.mak
DOC=gstreamer
FORMATS=html
html: html-build.stamp
sinclude $(srcdir)/../upload.mak

# generated basefiles
#basefiles = \
#		gstreamer-@GST_MAJORMINOR@.types \
#		gstreamer-@GST_MAJORMINOR@-sections.txt \
#		gstreamer-@GST_MAJORMINOR@-docs.sgml

# ugly hack to make -unused.sgml work
#unused-build.stamp:
#	BUILDDIR=`pwd` && \
#	cd $(srcdir)/tmpl && \
#	ln -sf gstreamer-unused.sgml \
#		$$BUILDDIR/tmpl/gstreamer-@GST_MAJORMINOR@-unused.sgml
#	touch unused-build.stamp

# these rules are added to create parallel docs using GST_MAJORMINOR
#$(basefiles): gstreamer-@GST_MAJORMINOR@%: gstreamer%
#	cp $< $@

#CLEANFILES = $(basefiles)

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(top_srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR=$(top_srcdir)/gst

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --ignore-files=trio

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=

# Used for dependencies.
HFILE_GLOB=$(DOC_SOURCE_DIR)/*.h
CFILE_GLOB=$(DOC_SOURCE_DIR)/*.c

# this is a wingo addition
# thomasvs: another nice wingo addition would be an explanation on why
# this is useful ;)

#SCANOBJ_DEPS = $(top_builddir)/gst/elements/libgstelements.la \
#	$(top_builddir)/gst/schedulers/libgstbasicomegascheduler.la

# Header files to ignore when scanning. Use base file name, no paths
IGNORE_HFILES= \
	gettext.h \
	gst-i18n-lib.h \
	gst-i18n-app.h \
	gst_private.h \
	gstatomic_impl.h \
	gstdata_private.h \
	gstarch.h \
	cothreads.h \
	cothreads_compat.h \
	gthread-cothreads.h \
	types.h \
	grammar.tab.h \
	gstmarshal.h \
	elements/gstaggregator.h \
	elements/gstbufferstore.h \
	elements/gstfakesink.h \
	elements/gstfakesrc.h \
	elements/gstfdsink.h \
	elements/gstfdsrc.h \
	elements/gstfilesink.h \
	elements/gstfilesrc.h \
	elements/gstidentity.h \
	elements/gstmd5sink.h \
	elements/gstmultifilesrc.h \
	elements/gstpipefilter.h \
	elements/gstshaper.h \
	elements/gststatistics.h \
	elements/gsttee.h \
	elements/gsttypefind.h

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(GST_CFLAGS) -I$(top_builddir)
GTKDOC_LIBS = $(GST_LIBS) $(SCANOBJ_DEPS)

GTKDOC_CC=$(LIBTOOL) --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt



###########################################################################
# Everything below here is generic and you shouldn't need to change it.
###########################################################################
# thomas: except of course that we did

# thomas: copied from glib-2
# We set GPATH here; this gives us semantics for GNU make
# which are more like other make's VPATH, when it comes to
# whether a source that is a target of one rule is then
# searched for in VPATH/GPATH.
#
GPATH = $(srcdir)

# thomas: make docs parallel installable
TARGET_DIR=$(HTML_DIR)/$(DOC_MODULE)-@GST_MAJORMINOR@

EXTRA_DIST = 				\
	$(content_files)		\
	$(extra_files)			\
	$(HTML_IMAGES)			\
	$(DOC_MAIN_SGML_FILE)		\
	$(DOC_MODULE).types		\
	$(DOC_MODULE)-sections.txt

DOC_STAMPS=scan-build.stamp tmpl-build.stamp sgml-build.stamp html-build.stamp \
	   $(srcdir)/tmpl.stamp $(srcdir)/sgml.stamp $(srcdir)/html.stamp

SCANOBJ_FILES = 		\
	$(DOC_MODULE).args 	\
	$(DOC_MODULE).hierarchy \
	$(DOC_MODULE).interfaces \
	$(DOC_MODULE).prerequisites \
	$(DOC_MODULE)-scan.o	\
	$(DOC_MODULE).signals

CLEANFILES =  $(SCANOBJ_FILES) $(DOC_MODULE)-unused.txt $(DOC_STAMPS)

if HAVE_GTK_DOC
all-local: html-build.stamp

#### scan ####

# in the case of non-srcdir builds, the built gst directory gets added
# to gtk-doc scanning; but only then, to avoid duplicates
scan-build.stamp: $(HFILE_GLOB) $(SCANOBJ_DEPS) $(basefiles)
	@echo '*** Scanning header files ***'
	if grep -l '^..*$$' $(srcdir)/$(DOC_MODULE).types > /dev/null ; then \
	    if test x"$(srcdir)" != x. ; then \
	        cp $(srcdir)/$(DOC_MODULE).types . ; \
	        chmod u+w $(DOC_MODULE).types ; \
	    fi ; \
	    CC="$(GTKDOC_CC)" LD="$(GTKDOC_LD)" CFLAGS="$(GTKDOC_CFLAGS)" LDFLAGS="$(GTKDOC_LIBS)" $(GTK_DOC_SCANOBJ) --type-init-func="gst_init(NULL,NULL)" --module=$(DOC_MODULE) ; \
	else \
	    cd $(srcdir) ; \
	    for i in $(SCANOBJ_FILES) ; do \
               test -f $$i || touch $$i ; \
	    done \
	fi
	if test "x$(top_srcdir)" != "x$(top_builddir)"; \
        then \
          export BUILT_OPTIONS="--source-dir=$(top_builddir)/gst"; \
        fi; \
	gtkdoc-scan \
		$(SCAN_OPTIONS) $(EXTRA_HFILES) \
		--module=$(DOC_MODULE) \
		--source-dir=$(DOC_SOURCE_DIR) \
		$$BUILT_OPTIONS \
		--ignore-headers="$(IGNORE_HFILES)"
	touch scan-build.stamp

$(DOC_MODULE)-decl.txt $(SCANOBJ_FILES): scan-build.stamp
	@true

#### templates ####

tmpl-build.stamp: $(DOC_MODULE)-decl.txt $(SCANOBJ_FILES) $(DOC_MODULE)-sections.txt $(DOC_OVERRIDES)
	@echo '*** Rebuilding template files ***'
	if test x"$(srcdir)" != x. ; then \
	    cp $(srcdir)/$(DOC_MODULE)-sections.txt . ; \
	    touch $(DOC_MODULE)-decl.txt ; \
	fi
	gtkdoc-mktmpl --module=$(DOC_MODULE) | tee tmpl-build.log
	@if grep "WARNING:" tmpl-build.log > /dev/null; then exit 1; fi
	rm -f tmpl-build.log
	touch tmpl-build.stamp

tmpl.stamp: tmpl-build.stamp
	@true

#### xml ####

sgml-build.stamp: tmpl.stamp $(CFILE_GLOB)
	@echo '*** Building XML ***'
	gtkdoc-mkdb --module=$(DOC_MODULE) --source-dir=$(DOC_SOURCE_DIR) --main-sgml-file=$(DOC_MAIN_SGML_FILE) --output-format=xml $(MKDB_OPTIONS) | tee sgml-build.log
	@if grep "WARNING:" sgml-build.log > /dev/null; then exit 1; fi
	rm sgml-build.log
	touch sgml-build.stamp

sgml.stamp: sgml-build.stamp
	@true

#### html ####

# FIXME: setting LANG=C is a hack to work with gtk-doc < 1.1, because
# that forces a non-utf8 locale.  gtk-doc >= 1.1 solves this by calling
# use bytes; in gtkdoc-fixxref
html-build.stamp: sgml.stamp $(DOC_MAIN_SGML_FILE) $(content_files)
	@echo '*** Building HTML ***'
	test -d html || mkdir html
	cd html && gtkdoc-mkhtml $(DOC_MODULE) $(srcdir)/../$(DOC_MAIN_SGML_FILE)
	test "x$(HTML_IMAGES)" = "x" || for i in "" $(HTML_IMAGES) ; do \
	    if test "$$i" != ""; then cp $(srcdir)/$$i html ; fi; done
	@echo '-- Fixing Crossreferences' 
	LANG=C && gtkdoc-fixxref --module-dir=html --html-dir=$(HTML_DIR) $(FIXXREF_OPTIONS)
	touch html-build.stamp
endif

clean-local:
	rm -f *~ *.bak
	rm -rf .libs

maintainer-clean-local: clean
	cd $(srcdir) && rm -rf xml html $(DOC_MODULE)-decl-list.txt $(DOC_MODULE)-decl.txt

# company: don't delete .sgml and -sections.txt as they're in CVS
# FIXME : thomas added all sgml files and some other things to make
# make distcheck work
distclean-local: clean
	rm -rf $(DOC_MODULE)-decl-list.txt
	rm -rf $(DOC_MODULE)-decl.txt
	rm -rf $(DOC_MODULE)-undocumented.txt
	rm -rf $(DOC_MODULE)-unused.txt
	rm -rf tmpl/*.sgml.bak
	rm -f $(DOC_MODULE).hierarchy
	rm -f *.stamp || true
	if test x"$(srcdir)" != x. ; then \
	    rm -f $(DOC_MODULE)-docs.sgml ; \
	    rm -f $(DOC_MODULE).types ; \
	    rm -f $(DOC_MODULE).interfaces ; \
	    rm -f $(DOC_MODULE).prerequisites ; \
	    rm -f $(DOC_MODULE)-sections.txt ; \
	    rm -rf tmpl/*.sgml ; \
	fi
	rm -rf *.o

# thomas: make docs parallel installable; devhelp requires majorminor too
if DOC_HTML
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
	fi) 
	if test -d $(DESTDIR)$(TARGET_DIR); then rmdir -p --ignore-fail-on-non-empty $(DESTDIR)$(TARGET_DIR); fi
else
install-data-local:
uninstall-local:
endif

#
# Require gtk-doc when making dist
#
if HAVE_GTK_DOC
dist-check-gtkdoc:
else
dist-check-gtkdoc:
	@echo "*** gtk-doc must be installed and enabled in order to make dist"
	@false
endif

dist-hook: dist-check-gtkdoc dist-hook-local
	mkdir $(distdir)/tmpl
	mkdir $(distdir)/xml
	mkdir $(distdir)/html
	-cp $(srcdir)/tmpl/*.sgml $(distdir)/tmpl
	-cp $(srcdir)/sgml/*.xml $(distdir)/xml
	-cp $(srcdir)/html/index.sgml $(distdir)/html
	-cp $(srcdir)/html/*.html $(srcdir)/html/*.css $(distdir)/html

	images=$(HTML_IMAGES) ;    	      \
	for i in "" $$images ; do		      \
	  if test "$$i" != ""; then cp $(srcdir)/$$i $(distdir)/html ; fi; \
	done

.PHONY : dist-hook-local
