
if BUILD_PLUGIN_DOCS
SUBDIRS_PLUGINS = plugins
else
SUBDIRS_PLUGINS =
endif

SUBDIRS = manual fwg gst libs $(SUBDIRS_PLUGINS) devhelp
DIST_SUBDIRS = manual fwg gst libs plugins xsl devhelp

EXTRA_DIST = slides manuals.mak

dist-hook:
	$(RM) -rf $(distdir)/random/CVS
	$(RM) -rf $(distdir)/random/*~
	$(RM) -rf $(distdir)/random/.\#*
	$(RM) -rf $(distdir)/slides/CVS
	$(RM) -rf $(distdir)/slides/*~
	$(RM) -rf $(distdir)/slides/.\#*
