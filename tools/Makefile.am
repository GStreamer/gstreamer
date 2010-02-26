bin_SCRIPTS = \
	gst-visualise-@GST_MAJORMINOR@

man_MANS = \
        gst-visualise-@GST_MAJORMINOR@.1

CLEANFILES = $(man_MANS) $(bin_SCRIPTS)

EXTRA_DIST = \
	gst-visualise-m.m gst-visualise.1.in

# generate versioned scripts from templates
%-@GST_MAJORMINOR@: %-m.m
	$(AM_V_GEN)sed -e s,\@GST_MAJORMINOR\@,@GST_MAJORMINOR@,g $< > $@ && \
	chmod +x $@

# generate man pages
%-@GST_MAJORMINOR@.1: %.1.in
	$(AM_V_GEN)sed \
                -e s,gst-visualise,gst-visualise-@GST_MAJORMINOR@,g \
                $< >$@

all: all-am chmod

chmod: $(bin_SCRIPTS)
	@chmod +x $^
