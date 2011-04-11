bin_SCRIPTS = \
	gst-visualise-@GST_MAJORMINOR@

man_MANS = \
        gst-visualise-@GST_MAJORMINOR@.1

bin_PROGRAMS = \
	gst-discoverer-@GST_MAJORMINOR@

gst_discoverer_@GST_MAJORMINOR@_SOURCES = gst-discoverer.c

CLEANFILES = $(man_MANS) $(bin_SCRIPTS) $(bin_PROGRAMS)

EXTRA_DIST = \
	gst-visualise-m.m gst-visualise.1.in


LDADD = $(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la\
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_MAJORMINOR@.la\
	$(GST_BASE_LIBS) $(GST_LIBS)

AM_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)

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

Android.mk: Makefile.am
	androgenizer -:PROJECT gst-discoverer \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:EXECUTABLE gst-discoverer -:TAGS eng debug \
	 -:SOURCES $(gst_discoverer_@GST_MAJORMINOR@_SOURCES) \
	 -:CFLAGS $(DEFS) $(AM_CFLAGS) \
	 -:LDFLAGS -lgstpbutils-@GST_MAJORMINOR@ -lgstvideo-@GST_MAJORMINOR@ \
	 	   $(GST_BASE_LIBS) $(GST_LIBS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
