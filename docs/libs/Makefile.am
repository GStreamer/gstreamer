## Process this file with automake to produce Makefile.in

# The name of the module.
DOC_MODULE=gstreamer-libs

# The top-level SGML file.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code (if it contains documentation).
DOC_SOURCE_DIR=$(top_srcdir)/libs

HTML_DIR=$(datadir)/gstreamer/html

TARGET_DIR=$(HTML_DIR)/$(DOC_MODULE)

if HAVE_GTK_DOC
html: html/book1.html
else
html:
endif

# rule to make the html 
html/book1.html: sgml/$(DOC_MODULE)-doc.bottom $(DOC_MAIN_SGML_FILE)
	if ! test -d html ; then mkdir html ; fi
	-cd html && gtkdoc-mkhtml $(DOC_MODULE) ../$(DOC_MAIN_SGML_FILE)

# rule to cause scanning to be done
$(DOC_MODULE)-decl.txt:
	gtkdoc-scan --module=$(DOC_MODULE) --source-dir=$(DOC_SOURCE_DIR) --ignore-headers=""

# rule to cause tmpls to be made
tmpl/$(DOC_MODULE)-unused.sgml: $(DOC_MODULE)-decl.txt $(DOC_MODULE)-sections.txt
	gtkdoc-mktmpl --module=$(DOC_MODULE)

# rule to make sgml
sgml/$(DOC_MODULE)-doc.bottom: tmpl/$(DOC_MODULE)-unused.sgml
	gtkdoc-mkdb --module=$(DOC_MODULE) --source-dir=$(DOC_SOURCE_DIR)

clean-local:
	$(RM) -rf *~ *.bak *.signals *-unused.txt *.args tmpl html sgml tmpl/*.bak $(DOC_MODULE)-decl-list.txt $(DOC_MODULE)-decl.txt 

install-data-local: html
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

dist-hook: html
	mkdir $(distdir)/html
	mkdir $(distdir)/sgml
	mkdir $(distdir)/tmpl
	-cp $(srcdir)/html/*.html $(srcdir)/html/*.css $(distdir)/html
	-cp $(srcdir)/tmpl/*.sgml $(distdir)/tmpl
	-cp $(srcdir)/sgml/*.sgml $(distdir)/sgml
	-cp $(srcdir)/sgml/*.bottom $(srcdir)/sgml/*.top $(distdir)/sgml

.PHONY : html
