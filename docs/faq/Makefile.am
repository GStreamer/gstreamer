DOC=gstreamer-faq
MAIN=$(DOC).xml
XML=$(wildcard *.xml)
XSLFO=$(srcdir)/../xsl/fo.xsl
XSLFOMODS=$(srcdir)/../xsl/ulink.xsl $(srcdir)/../xsl/keycombo.xsl
XSLHTML=$(srcdir)/../xsl/html.xsl
XSLHTMLMODS=$(srcdir)/../xsl/fileext.xsl $(srcdir)/../xsl/admon.xsl \
	$(srcdir)/../xsl/keycombo.xsl $(srcdir)/../xsl/css.xsl
XSLS=$(XSLFO) $(XSLFOMODS) $(XSLHTML) $(XSLHTMLMODS)
SRC=$(XML)
CSS=base.css

EXTRA_DIST = $(XML) $(CSS) magic-png magic-pdf

include $(srcdir)/../manuals.mak

# target for building the faq dir to be put on the website and rsync-ing it
www:	www-faq
	export RSYNC_RSH=ssh
	cd www-faq && rsync -arv * thomasvs@shell.sf.net:/home/groups/g/gs/gstreamer/htdocs/docs/faq/

www-faq: gstreamer-faq
	mkdir -p www-faq
	cp gstreamer-faq/base.css www-faq
	cd gstreamer-faq && for a in *.html; do \
		xmllint --format --htmlout $$a 2> /dev/null | \
                        tail +3 > ../www-faq/$$a; \
	done
	cd www-faq && rename html php *.html && for a in *.php; do \
		perl -i -p -e's@^.*\<head.*@\<?php include "../../lib/template.php"; echo get_header ("GStreamer: FAQ", "../.."); \?\>@' $$a; \
		perl -i -p -e's@^.*\</body.*@\<?php echo get_footer(); \?\>@' $$a; \
		perl -i -p -e's@^.*\</html.*@@' $$a; \
	done
	cd www-faq && for a in *.php; do \
		perl -i -p -e's@href="index\.html@href="index.php@' $$a; \
		perl -i -p -e's@href="(ar.*)\.html@href="$$1.php@' $$a; \
	done

check:
	xmllint -noout -valid $(MAIN)

