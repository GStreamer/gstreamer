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

$(manualname)/$(htmlname): $(sgml_files) $(png_files)
	db2html $(manualname).sgml

$(manualname).pdf: $(manualname).ps
	ps2pdf $< $@

$(manualname).ps: $(sgml_files) $(eps_files)
	db2ps $(manualname).sgml

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
pngdatadir   = $(datadir)/gstreamer/gstreamer-manual/images
pngdata_DATA = $(png_files)

htmlinst:
	@if [ -r $(manualname)/$(htmlname) ] ; then \
	echo "Installing $(manualname)/*.html" ; \
	    $(mkinstalldirs) $(DESTDIR)$(docdatadir)/$(manualname) ; \
	    $(INSTALL_DATA) $(manualname)/*.html $(DESTDIR)$(docdatadir)/$(manualname) ; \
	else \
	    if [ -r $(srcdir)/$(manualname)/$(htmlname) ] ; then \
	        echo "Installing $(srcdir)/$(manualname)/*.html" ; \
	        $(mkinstalldirs) $(DESTDIR)$(docdatadir)/$(manualname) ; \
	        $(INSTALL_DATA) $(srcdir)/$(manualname)/*.html $(DESTDIR)$(docdatadir)/$(manualname) ; \
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

