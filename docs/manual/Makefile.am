SUBDIRS = images .

DIST_SUBDIRS = images

EXTRA_DIST = fig \
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
             gstreamer-manual.sgml \
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

gstreamer-manual/gstreamer.html:
	db2html gstreamer-manual.sgml

gstreamer-manual.pdf:
	db2pdf gstreamer-manual.sgml

gstreamer-manual.ps:
	db2ps gstreamer-manual.sgml


PDFFILES=gstreamer-manual.pdf
PSFILES=gstreamer-manual.ps


htmldocs: gstreamer-manual/gstreamer.html gstreamer-manual/images

gstreamer-manual/images:
	ln -s ../images gstreamer-manual

htmldist: 
	@if [ -r gstreamer-manual/gstreamer.html ] ; then \
	    echo "Adding HTML manual to distribution" ; \
	    $(mkinstalldirs) $(distdir)/gstreamer-manual ; \
	    cp -a gstreamer-manual/* $(distdir)/gstreamer-manual/ ; \
	else \
	    echo "Skipping HTML manual from distribution: can't build" ; \
	fi

pdfdocs: $(PDFFILES)
pdfdist:
	@if [ -r gstreamer-manual.pdf ] ; then \
	    echo "Adding PDF manual to distribution" ; \
	    cp -a gstreamer-manual.pdf $(distdir)/gstreamer-manual.pdf ; \
	else \
	    echo "Skipping PDF manual from distribution: can't build" ; \
	fi

psdocs: $(PSFILES)
psdist:
	@if [ -r gstreamer-manual.ps ] ; then \
	    echo "Adding PS manual to distribution" ; \
	    cp -a gstreamer-manual.ps $(distdir)/gstreamer-manual.ps ; \
	else \
	    echo "Skipping PS manual from distribution: can't build" ; \
	fi

# Data to install, in the usual automake way
docdatadir   = $(datadir)/gstreamer
docdata_DATA = $(PDFFILES) $(PSFILES)

htmlinst:
	@if [ -r gstreamer-manual/gstreamer.html ] ; then \
	echo "Installing HTML documentation" ; \
	    $(mkinstalldirs) $(DESTDIR)$(docdatadir)/gstreamer-manual ; \
	    $(mkinstalldirs) $(DESTDIR)$(docdatadir)/gstreamer-manual/images ; \
	    $(INSTALL_DATA) gstreamer-manual/*.html $(DESTDIR)$(docdatadir)/gstreamer-manual ; \
	else \
	    if [ -r $(srcdir)/gstreamer-manual/gstreamer.html ] ; then \
	        echo "Installing HTML documentation" ; \
	        $(mkinstalldirs) $(DESTDIR)$(docdatadir)/gstreamer-manual ; \
	        $(mkinstalldirs) $(DESTDIR)$(docdatadir)/gstreamer-manual/images ; \
	        $(INSTALL_DATA) $(srcdir)/gstreamer-manual/*.html $(DESTDIR)$(docdatadir)/gstreamer-manual ; \
	    else \
	        echo "NOT installing HTML documentation: not present, and can't generate" ; \
	    fi \
	fi

all-local: htmldocs pdfdocs psdocs
dist-hook: htmldist pdfdist psdist
install-data-local: htmlinst

