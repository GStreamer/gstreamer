## Process this file with automake to produce Makefile.in

# The name of the module.
DOC_MODULE=gstreamer-plugins

# The top-level SGML file.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code (if it contains documentation).
DOC_SOURCE_DIR=$(top_srcdir)/plugins

#CFLAGS = `gstreamer-config --cflags` -Wall -g
#LDFLAGS = `gstreamer-config --libs` 

EXTRA_DIST=$(DOC_MODULE).types.in $(DOC_MODULE)-sections.txt $(DOC_MAIN_SGML_FILE) gstdoc-mkdb gstdoc-mktmpl gstdoc-scanobj

TARGET_DIR=$(HTML_DIR)/$(DOC_MODULE)

tmpl_sources =				\
	tmpl/videoraw.sgml

gstreamer_doc_DATA =		\
	$(DOC_MODULE).html		\
	$(DOC_MODULE).hierarchy	\
	$(DOC_MODULE).types		\
	$(DOC_MODULE)-sections.txt

SCANOBJS_FILES =		\
	$(DOC_MODULE).signals	\
	$(DOC_MODULE).hierarchy	\
	$(DOC_MODULE).args

if HAVE_GTK_DOC
$(DOC_MODULE).html: html/book1.html
	-cd $(srcdir) && cp html/book1.html $(DOC_MODULE).html
else
$(DOC_MODULE).html:
endif

html/book1.html: sgml/$(DOC_MODULE)-doc.bottom
	$(MAKE) html

sgml/$(DOC_MODULE)-doc.bottom: $(tmpl_sources)
	$(MAKE) sgml

scanobj:
       env CC="$(LIBTOOL) $(CC)" CFLAGS="$(LIBGST_CFLAGS) $(GLIB_CFLAGS) $(XML_CFLAGS) -I../../" LDFLAGS="$(GST_LIBS)"\
       ./gstdoc-scanobj --module=$(DOC_MODULE)

tmpl: scanobj 
	./gstdoc-mktmpl --module=$(DOC_MODULE)

sgml: tmpl
	./gstdoc-mkdb --module=$(DOC_MODULE) --source-dir=$(DOC_SOURCE_DIR)

html:
	if ! test -d html ; then mkdir html ; fi
	-cd html && gtkdoc-mkhtml $(DOC_MODULE) ../$(DOC_MAIN_SGML_FILE)

clean-local:
	rm -f *~ *.bak *.signals *-unused.txt *.args

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
	if ! test -d $(distdir)/tmpl ; then mkdir $(distdir)/tmpl ; fi
	-cp $(srcdir)/tmpl/*.sgml $(distdir)/tmpl

.PHONY : html sgml templates 
