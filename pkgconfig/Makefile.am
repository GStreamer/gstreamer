if USE_GCONF
GCONF_PC=gstreamer-gconf-@GST_MAJORMINOR@.pc
GCONF_PC_UNINSTALLED=gstreamer-gconf-@GST_MAJORMINOR@-uninstalled.pc
else
GCONF_PC=
GCONF_PC_UNINSTALLED=
endif

### all of the standard pc files we need to generate
pcfiles =  \
	gstreamer-libs-@GST_MAJORMINOR@.pc \
	gstreamer-play-@GST_MAJORMINOR@.pc \
	gstreamer-interfaces-@GST_MAJORMINOR@.pc
pcfiles_uninstalled = \
	gstreamer-libs-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-play-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-interfaces-@GST_MAJORMINOR@-uninstalled.pc

pcfiles_gconf = $(GCONF_PC) $(GCONF_PC_UNINSTALLED)
all-local: $(pcfiles) $(pcfiles_uninstalled) $(pcfiles_gconf)

### how to generate pc files from .pc files in this dir
$(pcfiles): %-@GST_MAJORMINOR@.pc: %.pc
	cp $< $@
$(pcfiles_uninstalled): %-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	cp $< $@

### how to generate gconf dir pc files from their pc.in files
### somebody smart could figure out how to fold this back into one rule
$(top_builddir)/gst-libs/gst/gconf/gstreamer-gconf*.pc:
	cd $(top_builddir)/gst-libs/gst/gconf && make gstreamer-gconf.pc gstreamer-gconf-uninstalled.pc

### how to generate pc files from base .pc file in other dir
$(pcfiles_gconf): gstreamer-gconf-@GST_MAJORMINOR@%.pc: \
	$(top_builddir)/gst-libs/gst/gconf/gstreamer-gconf%.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = \
	$(GCONF_PC) \
	gstreamer-libs-@GST_MAJORMINOR@.pc \
	gstreamer-play-@GST_MAJORMINOR@.pc \
	gstreamer-interfaces-@GST_MAJORMINOR@.pc

CLEANFILES = $(pcfiles) $(pcfiles_uninstalled) $(GCONF_PC) $(GCONF_PC_UNINSTALLED)
EXTRA_DIST= \
           gstreamer-libs.pc.in gstreamer-libs-uninstalled.pc.in \
           gstreamer-play.pc.in gstreamer-play-uninstalled.pc.in
