manualname = gstreamer-manual

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

eps_files =  bin-element.eps \
             connected-elements.eps \
             filter-element-multi.eps \
             filter-element.eps \
             hello-world.eps \
             mime-world.eps \
             queue.eps \
             sink-element.eps \
             src-element.eps \
             state-diagram.eps \
             thread.eps

png_files =  bin-element.png \
             connected-elements.png \
             filter-element-multi.png \
             filter-element.png \
             hello-world.png \
             mime-world.png \
             queue.png \
             sink-element.png \
             src-element.png \
             state-diagram.png \
             thread.png


EXTRA_DIST = $(sgml_files) $(fig_files)

PDFFILES=$(manualname).pdf
PSFILES=$(manualname).ps

$(manualname)/gstreamer.html: $(sgml_files) $(png_files)
	db2html $(manualname).sgml

$(manualname).pdf: $(manualname).ps
	ps2pdf $< $@

$(manualname).ps: $(sgml_files) $(eps_files)
	db2ps $(manualname).sgml

%.png : %.fig
	fig2dev -L png -s 16 $< $@

%.eps : %.fig
	fig2dev -L eps -s 16 -m 0.5 $< $@

$(manualname)/images:
	ln -s .. $(manualname)/images

htmldocs: $(manualname)/gstreamer.html $(manualname)/images
htmldist: htmldocs
	@if [ -r $(manualname)/gstreamer.html ] ; then \
	    echo "Adding $(manualname)/gstreamer.html to distribution" ; \
	    $(mkinstalldirs) $(distdir)/$(manualname) ; \
	    cp -a $(manualname)/*.html $(distdir)/$(manualname)/ ; \
	else \
	    echo "Skipping HTML manual from distribution: can't build" ; \
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
	@for a $(PSFILES) ; do \
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
	@if [ -r $(manualname)/gstreamer.html ] ; then \
	echo "Installing $(manualname)/*.html" ; \
	    $(mkinstalldirs) $(DESTDIR)$(docdatadir)/$(manualname) ; \
	    $(INSTALL_DATA) $(manualname)/*.html $(DESTDIR)$(docdatadir)/$(manualname) ; \
	else \
	    if [ -r $(srcdir)/$(manualname)/gstreamer.html ] ; then \
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
	$(RM) -rf *.png *.eps $(manualname)/ $(manualname).ps $(manualname).pdf
dist-hook: htmldist pdfdist psdist
install-data-local: htmlinst
uninstall-local: htmluninst

