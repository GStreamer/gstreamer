schemadir = @GCONF_SCHEMA_FILE_DIR@
schema_DATA = gstreamer.schemas

install-data-local:
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) \
		--makefile-install-rule $(srcdir)/$(schema_DATA)

