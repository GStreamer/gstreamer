manualname = gstreamer-manual
htmlname = gstreamer.html

sgml_files = gstreamer-manual.sgml \
	     advanced.sgml \
             bins.sgml \
             buffers.sgml \
             components.sgml \
             connections.sgml \
             cothreads.sgml \
             dynamic.sgml \
             elements.sgml \
             factories.sgml \
             ghostpads.sgml \
             goals.sgml \
             helloworld.sgml \
             helloworld2.sgml \
             intro.sgml \
             motivation.sgml \
             plugins.sgml \
             programs.sgml \
             queues.sgml \
             states.sgml \
             threads.sgml \
             typedetection.sgml \
             utility.sgml \
             xml.sgml

fig_files =  bin-element.fig \
             connected-elements.fig \
             filter-element-multi.fig \
             filter-element.fig \
             hello-world.fig \
             mime-world.fig \
             queue.fig \
             sink-element.fig \
             src-element.fig \
             state-diagram.fig \
             thread.fig

eps_files =  images/bin-element.eps \
             images/connected-elements.eps \
             images/filter-element-multi.eps \
             images/filter-element.eps \
             images/hello-world.eps \
             images/mime-world.eps \
             images/queue.eps \
             images/sink-element.eps \
             images/src-element.eps \
             images/state-diagram.eps \
             images/thread.eps

png_files =  images/bin-element.png \
             images/connected-elements.png \
             images/filter-element-multi.png \
             images/filter-element.png \
             images/hello-world.png \
             images/mime-world.png \
             images/queue.png \
             images/sink-element.png \
             images/src-element.png \
             images/state-diagram.png \
             images/thread.png


EXTRA_DIST = $(sgml_files) $(fig_files)

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
	ln -s ../images $(manualname)/images

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
	$(RM) -rf $(manualname)/ images/*.eps images/*.png *.eps *.png *.ps *.pdf *.aux *.dvi *.log *.tex
dist-hook: htmldist pdfdist psdist
install-data-local: htmlinst
uninstall-local: htmluninst

