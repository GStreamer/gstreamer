## Process this file with automake to produce Makefile.in

# The name of the module.
DOC_MODULE=gstreamer

# The top-level SGML file.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code (if it contains documentation).
DOC_SOURCE_DIR=$(top_srcdir)/gst

EXTRA_DIST = gstreamer.types.in gstreamer.hierarchy $(DOC_MODULE)-sections.txt $(DOC_MAIN_SGML_FILE)

HTML_DIR=$(datadir)/gstreamer/html

GST_LIBS += ../../gst/elements/libgstelements.la


TARGET_DIR=$(HTML_DIR)/$(DOC_MODULE)

tmpl_sources =				\
	tmpl/audioraw.sgml              \
	tmpl/cothreads.sgml             \
	tmpl/gst.sgml                   \
	tmpl/gstinfo.sgml               \
	tmpl/gstasyncdisksrc.sgml       \
	tmpl/gstautoplug.sgml           \
	tmpl/gstscheduler.sgml          \
	tmpl/gstprops.sgml              \
	tmpl/gstcaps.sgml               \
	tmpl/gstaudiosink.sgml          \
	tmpl/gstaudiosrc.sgml           \
	tmpl/gstbin.sgml                \
	tmpl/gstbuffer.sgml             \
	tmpl/gstbufferpool.sgml         \
	tmpl/gstdisksrc.sgml            \
	tmpl/gstelement.sgml            \
	tmpl/gstfakesink.sgml           \
	tmpl/gstfakesrc.sgml            \
	tmpl/gstfdsink.sgml             \
	tmpl/gstfdsrc.sgml              \
	tmpl/gsthttpsrc.sgml            \
	tmpl/gstidentity.sgml           \
	tmpl/gstlog.sgml                \
	tmpl/gstmeta.sgml               \
	tmpl/gstobject.sgml             \
	tmpl/gstpad.sgml                \
	tmpl/gstpipeline.sgml           \
	tmpl/gstplugin.sgml             \
	tmpl/gstqueue.sgml              \
	tmpl/gstreamer-unused.sgml      \
	tmpl/gstsinesrc.sgml            \
	tmpl/gsttee.sgml                \
	tmpl/gstthread.sgml             \
	tmpl/gsttrace.sgml              \
	tmpl/gsttype.sgml               \
	tmpl/gstutils.sgml              \
	tmpl/gstxml.sgml                \
	tmpl/spectrum.sgml              \
	tmpl/videoraw.sgml

gstreamer_docdir = $(HTML_DIR)
gstreamer_doc_DATA =		\
	gstreamer.html		\
	gstreamer.types		\
	gstreamer.hierarchy	\
	gstreamer-sections.txt

SCANOBJS_FILES =		\
	$(DOC_MODULE).signals	\
	$(DOC_MODULE).hierarchy	\
	$(DOC_MODULE).args

if HAVE_GTK_DOC
gstreamer.html: html/book1.html
	-cd $(srcdir) && cp html/book1.html gstreamer.html
else
gstreamer.html:
endif

html/book1.html: sgml/gstreamer-doc.bottom
	$(MAKE) html

sgml/gstreamer-doc.bottom: $(tmpl_sources)
	$(MAKE) sgml

gstreamer-decl.txt:
	gtkdoc-scan --module=$(DOC_MODULE) --source-dir=$(DOC_SOURCE_DIR) --ignore-headers=""

scanobj: $(srcdir)/$(DOC_MODULE).types
	rm gstreamer-scan.o; ln -s gstreamer-scan.lo gstreamer-scan.o
	env CC="$(LIBTOOL) $(CC)" CFLAGS="$(GST_CFLAGS) $(GLIB_CFLAGS) $(XML_CFLAGS) -I../../" LDFLAGS="$(GST_LIBS)"\
	gtkdoc-scanobj --module=$(DOC_MODULE) 
	gtkdoc-scan --module=$(DOC_MODULE) --source-dir=$(DOC_SOURCE_DIR)

tmpl: scanobj gstreamer-decl.txt
	gtkdoc-mktmpl --module=$(DOC_MODULE)

sgml: tmpl
	gtkdoc-mkdb --module=$(DOC_MODULE) --source-dir=$(DOC_SOURCE_DIR)

html: sgml
	if ! test -d html ; then mkdir html ; fi
	-cd html && gtkdoc-mkhtml $(DOC_MODULE) ../$(DOC_MAIN_SGML_FILE)

clean-local:
	rm -f *~ *.bak *.signals *-unused.txt *.args

maintainer-clean-local: clean
	rm -rf sgml html $(DOC_MODULE)-decl-list.txt $(DOC_MODULE)-decl.txt 

install-data-local: gstreamer.html
	$(mkinstalldirs) $(DESTDIR)$(TARGET_DIR)
	(installfiles=`echo $(srcdir)/html/*.html`; \
	if test "$$installfiles" = '$(srcdir)/html/*.html'; \
	then echo '-- Nothing to install' ; \
	else \
	  for i in $$installfiles; do \
	    echo '-- Installing '$$i ; \
	    $(INSTALL_DATA) $$i $(DESTDIR)$(TARGET_DIR); \
	  done; \
	  echo '-- Installing $(srcdir)/html/index.sgml' ; \
	  $(INSTALL_DATA) $(srcdir)/html/index.sgml $(DESTDIR)$(TARGET_DIR); \
	  echo '-- Fixing Crossreferences' ; \
	  gtkdoc-fixxref --module=$(DOC_MODULE) --html-dir=$(HTML_DIR)|| true; \
	fi)

dist-hook: gstreamer.html
	mkdir $(distdir)/html
	mkdir $(distdir)/sgml
	mkdir $(distdir)/tmpl
	-cp $(srcdir)/html/*.html $(srcdir)/html/*.css $(distdir)/html
	-cp $(srcdir)/tmpl/*.sgml $(distdir)/tmpl
	-cp $(srcdir)/sgml/*.sgml $(distdir)/sgml
	-cp $(srcdir)/sgml/*.bottom $(srcdir)/sgml/*.top $(distdir)/sgml

.PHONY : html sgml tmpl
