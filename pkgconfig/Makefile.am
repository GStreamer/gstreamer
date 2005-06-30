if USE_GCONF
GCONF_PC=gstreamer-gconf-@GST_MAJORMINOR@.pc
GCONF_PC_UNINSTALLED=gstreamer-gconf-@GST_MAJORMINOR@-uninstalled.pc
else
GCONF_PC=
GCONF_PC_UNINSTALLED=
endif

### all of the standard pc files we need to generate
pcverfiles =  \
	$(GCONF_PC) \
	gstreamer-plugins-base-@GST_MAJORMINOR@.pc
pcverfiles_uninstalled = \
	$(GCONF_PC_UNINSTALLED) \
	gstreamer-plugins-base-@GST_MAJORMINOR@-uninstalled.pc

pcverfiles_gconf = $(GCONF_PC) $(GCONF_PC_UNINSTALLED)
all-local: $(pcverfiles) $(pcverfiles_uninstalled)

### how to generate versioned .pc files from .pc files in this dir
%-@GST_MAJORMINOR@.pc: %.pc
	cp $< $@
%-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcverfiles)

CLEANFILES = $(pcverfiles) $(pcverfiles_uninstalled)
pcinfiles = \
           gstreamer-gconf.pc.in gstreamer-gconf-uninstalled.pc.in \
           gstreamer-plugins-base.pc.in gstreamer-plugins-base-uninstalled.pc.in

DISTCLEANFILES = $(pcinfiles:.in=)
EXTRA_DIST = $(pcinfiles)
