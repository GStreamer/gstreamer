### this is the part you can customize if you need to

# base name of doc
DOC = faq

# main xml file
MAIN = $(DOC).xml
# all xml sources
XML = $(notdir $(wildcard $(srcdir)/*.xml))
# base style sheet
CSS = base.css

# image sources
PNG_SRC = 
FIG_SRC = $(notdir $(wildcard $(srcdir)/*.fig))

### this is the generic bit and you shouln't need to change this

# get the generic docbuilding Makefile stuff
include $(srcdir)/../manuals.mak

### this is standard automake stuff
                                                                                
# package up all the source
EXTRA_DIST = $(SRC)
                                                                                
# install documentation
docdir = $(datadir)/doc/$(PACKAGE)-$(VERSION)/$(DOC)
doc_DATA = $(PDF_DAT) $(PS_DAT)
                                                                                
include $(srcdir)/../htmlinstall.mak

### these are website updating targets
# target for building the faq dir to be put on the website and rsync-ing it
upload:   www
	export RSYNC_RSH=ssh
	cd www && rsync -arv * thomasvs@shell.sf.net:/home/groups/g/gs/gstreamer/htdocs/docs/faq/

www: html
	mkdir -p www
	cp html/base.css www
	cd html && for a in *.html; do \
		xmllint --format --htmlout $$a 2> /dev/null | \
		tail +3 > ../www/$$a; \
	done
	cd www && rename html php *.html && for a in *.php; do \
		perl -i -p -e's@^.*\<head.*@\<?php include "../../lib/template.php"; echo get_header ("GStreamer: FAQ", "../.."); \?\>@' $$a; \
		perl -i -p -e's@^.*\</body.*@\<?php echo get_footer(); \?\>@' $$a; \
		perl -i -p -e's@^.*\</html.*@@' $$a; \
	done
	cd www && for a in *.php; do \
		perl -i -p -e's@href="index\.html@href="index.php@' $$a; \
		perl -i -p -e's@href="(ar.*)\.html@href="$$1.php@' $$a; \
	done
	@-cp -pr html/images www 2> /dev/null

