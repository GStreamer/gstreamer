
if BUILD_PLUGIN_DOCS
SUBDIRS_PLUGINS = plugins
else
SUBDIRS_PLUGINS =
endif

SUBDIRS = manual fwg gst libs $(SUBDIRS_PLUGINS)
DIST_SUBDIRS = manual fwg gst libs plugins xsl

EXTRA_DIST = random slides manuals.mak

dist-hook:
	$(RM) -rf $(distdir)/random/CVS
	$(RM) -rf $(distdir)/random/*~
	$(RM) -rf $(distdir)/random/.\#*
	$(RM) -rf $(distdir)/slides/CVS
	$(RM) -rf $(distdir)/slides/*~
	$(RM) -rf $(distdir)/slides/.\#*
