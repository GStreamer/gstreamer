## Process this file with automake to produce Makefile.in

# The name of the module.
DOC_MODULE=gstreamer

# The top-level SGML file.
DOC_MAIN_SGML_FILE=gstreamer-docs.sgml

# The directory containing the source code (if it contains documentation).
DOC_SOURCE_DIR=$(top_srcdir)/gst

CFLAGS = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir)
LDFLAGS = $(GLIB_LIBS) $(GTK_LIBS) $(top_srcdir)/gst/libgst.la

HTML_DIR=$(datadir)/gstreamer/gst/html

TARGET_DIR=$(HTML_DIR)/$(DOC_MODULE)

tmpl_sources =				\
	tmpl/gst.sgml			\
	tmpl/gstbin.sgml		\
	tmpl/gstbuffer.sgml		\
	tmpl/gstconnection.sgml		\
	tmpl/gstelement.sgml		\
	tmpl/gstfilter.sgml		\
	tmpl/gstmeta.sgml		\
	tmpl/gstobject.sgml		\
	tmpl/gstpad.sgml		\
	tmpl/gstpipeline.sgml		\
	tmpl/gstplugin.sgml		\
	tmpl/gstqueue.sgml		\
	tmpl/gstsink.sgml		\
	tmpl/gstsrc.sgml		\
	tmpl/gsttee.sgml		\
	tmpl/gstthread.sgml		\
	tmpl/gsttrace.sgml		\
	tmpl/gsttype.sgml		\
	tmpl/gstutils.sgml		\
	tmpl/gstxml.sgml		\
	tmpl/plugin.sgml

gstreamer_docdir = $(HTML_DIR)
gstreamer_doc_DATA =		\
	gstreamer.html		\
	gstreamer.hierarchy	\
	gstreamer.types		\
	gstreamer-decl.txt	\
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

scanobj:
	env CC="$(LIBTOOL) --mode=link $(CC)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" \
		gtkdoc-scanobj --module=$(DOC_MODULE) \
		--types=$(srcdir)/$(DOC_MODULE).types

scan:
	gtkdoc-scan --module=$(DOC_MODULE) --source-dir=$(DOC_SOURCE_DIR) --ignore-headers="gsttypefind.h"

tmpl: scan
	gtkdoc-mktmpl --module=$(DOC_MODULE)

sgml:
	gtkdoc-mkdb --module=$(DOC_MODULE) --source-dir=$(DOC_SOURCE_DIR)

html:
	if ! test -d html ; then mkdir html ; fi
	-cd html && gtkdoc-mkhtml $(DOC_MODULE) ../$(DOC_MAIN_SGML_FILE)

clean-local:
	rm -f *~ *.bak *.signals *-unused.txt

maintainer-clean-local: clean
	rm -rf sgml html $(DOC_MODULE)-decl-list.txt $(DOC_MODULE)-decl.txt 

install-data-local:
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

dist-hook:
	mkdir $(distdir)/html
	mkdir $(distdir)/sgml
	mkdir $(distdir)/tmpl
	-cp $(srcdir)/html/*.html $(srcdir)/html/*.css $(distdir)/html
	-cp $(srcdir)/tmpl/*.sgml $(distdir)/tmpl
	-cp $(srcdir)/sgml/*.sgml $(distdir)/sgml
	-cp $(srcdir)/sgml/*.bottom $(srcdir)/sgml/*.top $(distdir)/sgml

.PHONY : html sgml templates scan
