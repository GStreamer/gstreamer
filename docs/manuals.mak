# Included by directories containing manuals.
# Expects the following to be defined:
# manualname
# htmlname
# sgml_files
# fig_files
# eps_files
# png_files

PDFFILES=$(manualname).pdf
PSFILES=$(manualname).ps

if HAVE_FIG2DEV
$(manualname)/$(htmlname): $(sgml_files) $(png_files)
else
$(manualname)/$(htmlname): $(sgml_files)
endif
if HAVE_DB2HTML
	db2html $(manualname).sgml
else
	echo "Can't build $@: don't have db2html tool"
endif

$(manualname).pdf: $(manualname).ps
if HAVE_PS2PDF
	@if [ -r $< ] ; then ps2pdf $< $@ ; fi
else
	echo "Can't build $@: don't have ps2pdf tool"
endif

if HAVE_FIG2DEV
$(manualname).ps: $(sgml_files) $(eps_files)
else
$(manualname).ps: $(sgml_files)
endif
if HAVE_PS2PDF
	@if [ -r $< ] ; then db2ps $(manualname).sgml ; fi
else
	echo "Can't build $@: don't have db2ps tool"
endif

images :
	mkdir images

images/%.png : %.fig images
	fig2dev -L png -s 16 $< $@

images/%.eps : %.fig images
	fig2dev -L eps -s 16 -m 0.5 $< $@

$(manualname)/images:
	@ln -sf ../images $(manualname)/images

htmldocs: $(manualname)/$(htmlname) $(manualname)/images
htmldist: htmldocs
	@if [ -r $(manualname)/$(htmlname) ] ; then \
	    echo "Adding $(manualname)/$(htmlname) to distribution" ; \
	    $(mkinstalldirs) $(distdir)/$(manualname) ; \
	    cp -a $(manualname)/*.html $(distdir)/$(manualname)/ ; \
	else \
	    echo "Skipping $(manualname)/$(htmlname) from distribution: can't build" ; \
	fi

pdfdocs: $(PDFFILES)
pdfdist: pdfdocs
	@for a in $(PDFFILES) ; do \
	if [ -r $$a ] ; then \
	    echo "Adding $$a to distribution" ; \
	    cp -a $$a $(distdir)/ ; \
	else \
	    echo "Skipping $$a from distribution: can't build" ; \
	fi \
	done

psdocs: $(PSFILES)
psdist: psdocs
	@for a in $(PSFILES) ; do \
	if [ -r $$a ] ; then \
	    echo "Adding $$a to distribution" ; \
	    cp -a $$a $(distdir)/ ; \
	else \
	    echo "Skipping $$a from distribution: can't build" ; \
	fi \
	done

# Data to install, in the usual automake way
docdatadir   = $(datadir)/gstreamer
docdata_DATA = $(PDFFILES) $(PSFILES)

htmlinst:
	@if [ -r $(manualname)/$(htmlname) ] ; then \
	    echo "Installing $(manualname)" ; \
	    $(mkinstalldirs) $(DESTDIR)$(docdatadir)/$(manualname) ; \
	    $(mkinstalldirs) $(DESTDIR)$(docdatadir)/$(manualname)/images ; \
	    $(INSTALL_DATA) $(manualname)/*.html $(DESTDIR)$(docdatadir)/$(manualname) ; \
	    for a in "x" $(png_files); do \
	    if [ "x$$a" != "xx" ] ; then \
	    if [ -r $$a ] ; then \
	    $(INSTALL_DATA) $$a $(DESTDIR)$(docdatadir)/$(manualname)/images ; \
	    fi; fi; done \
	else \
	    if [ -r $(srcdir)/$(manualname)/$(htmlname) ] ; then \
	        echo "Installing $(srcdir)/$(manualname)" ; \
	        $(mkinstalldirs) $(DESTDIR)$(docdatadir)/$(manualname) ; \
		$(mkinstalldirs) $(DESTDIR)$(docdatadir)/$(manualname)/images ; \
	        $(INSTALL_DATA) $(srcdir)/$(manualname)/*.html $(DESTDIR)$(docdatadir)/$(manualname) ; \
		for a in "x" $(png_files); do \
		if [ "x$$a" != "xx" ] ; then \
		if [ -r $$a ] ; then \
		$(INSTALL_DATA) $$a $(DESTDIR)$(docdatadir)/$(manualname)/images ; \
		fi; fi; done \
	    else \
	        echo "NOT installing HTML documentation: not present, and can't generate" ; \
	    fi \
	fi

htmluninst:
	$(RM) -rf $(DESTDIR)$(docdatadir)/$(manualname)

all-local: htmldocs pdfdocs psdocs
clean-local:
	$(RM) -rf $(manualname)/ $(manualname).junk/ images/*.eps images/*.png *.eps *.png *.ps *.pdf *.aux *.dvi *.log *.tex DBTOHTML_OUTPUT_DIR*
dist-hook: htmldist pdfdist psdist
install-data-local: htmlinst
uninstall-local: htmluninst

