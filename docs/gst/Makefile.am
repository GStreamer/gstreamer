## Process this file with automake to produce Makefile.in

# The name of the module.
DOC_MODULE=gstreamer

# The top-level SGML file.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code (if it contains documentation).
DOC_SOURCE_DIR=$(top_srcdir)/gst

# FIXME :
# there's something wrong with gstreamer-sections.txt not being in the dist
# maybe it doesn't resolve; we're adding it below for now
EXTRA_DIST = gstreamer.types.in gstreamer.hierarchy $(DOC_MODULE)-sections.txt gstreamer-sections.txt $(DOC_MAIN_SGML_FILE)

HTML_DIR=$(datadir)/gstreamer/html

GST_LIBS += $(top_builddir)/gst/elements/libgstelements.la \
	    $(top_builddir)/gst/autoplug/libgststaticautoplug.la \
	    $(top_builddir)/gst/autoplug/libgststaticautoplugrender.la


TARGET_DIR=$(HTML_DIR)/$(DOC_MODULE)

tmpl_sources =				\
	tmpl/cothreads.sgml             \
	tmpl/gst.sgml                   \
	tmpl/gstinfo.sgml               \
	tmpl/gstautoplug.sgml           \
	tmpl/gstscheduler.sgml          \
	tmpl/gstprops.sgml              \
	tmpl/gstcaps.sgml               \
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
	tmpl/spectrum.sgml              

gstreamer_docdir = $(HTML_DIR)
gstreamer_doc_DATA =		\
	gstreamer.types		\
	gstreamer.hierarchy	\
	gstreamer-sections.txt

SCANOBJS_FILES =		\
	$(DOC_MODULE).signals	\
	$(DOC_MODULE).hierarchy	\
	$(DOC_MODULE).args

# FIXME: the dependencies here are rather broken.  Really, html/book1.html
# should be rebuilt if any file in $(top_srcdir)/gst is updated.
if HAVE_GTK_DOC
html/book1.html: 
	$(MAKE) html
else
html/book1.html:
endif

gstreamer-decl.txt:
	gtkdoc-scan --module=$(DOC_MODULE) --source-dir=$(DOC_SOURCE_DIR) --ignore-headers=""

# The LD env-var below is a nasty hack to make this work on versions of
# gtkdoc-scanobj which don't understand libtool
scanobj: $(srcdir)/$(DOC_MODULE).types
	cd ../../gst/;$(MAKE) libgst.la
	cd ../../gst/elements/;$(MAKE) libgstelements.la
	rm gstreamer-scan.o; ln -s gstreamer-scan.lo gstreamer-scan.o
	env CC="$(LIBTOOL) $(CC)" LD="cp gstreamer-scan.lo gstreamer-scan.o;$(LIBTOOL) $(CC)" CFLAGS="$(LIBGST_CFLAGS) $(GLIB_CFLAGS) $(XML_CFLAGS) -I../../" LDFLAGS="$(GST_LIBS)"\
	gtkdoc-scanobj --module=$(DOC_MODULE) 
	gtkdoc-scan --module=$(DOC_MODULE) --source-dir=$(DOC_SOURCE_DIR)

# FIXME: this is nasty, we shouldn't have to do this, HELP
tmpl: scanobj gstreamer-decl.txt
	cp $(top_srcdir)/docs/gst/gstreamer-sections.txt .
	gtkdoc-mktmpl --module=$(DOC_MODULE)

sgml: tmpl
	gtkdoc-mkdb --module=$(DOC_MODULE) --source-dir=$(DOC_SOURCE_DIR)

html: sgml
	if ! test -d html ; then mkdir html ; fi
	-cd html && gtkdoc-mkhtml $(DOC_MODULE) ../$(DOC_MAIN_SGML_FILE)

clean-local:
	rm -rf *~ *.bak *.signals *-unused.txt *.args *.o *.lo .libs sgml html

distclean-local: clean
	rm -rf $(DOC_MODULE)-decl-list.txt

install-data-local: html/book1.html
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

dist-hook: html/book1.html
	if ! test -d $(distdir)/tmpl ; then mkdir $(distdir)/tmpl ; fi
	-cp $(srcdir)/tmpl/*.sgml $(distdir)/tmpl

.PHONY : html sgml tmpl
