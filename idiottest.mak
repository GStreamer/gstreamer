# Idiot test to stop the installing of versions with plugin builddir enabled
install-exec-local:
if PLUGINS_USE_BUILDDIR
	@echo "*** ERROR: Cannot install:"
	@echo "GStreamer was configured using the --enable-plugin-builddir option."
	@echo
	@echo "This option is for development purposes only: binaries built with"
	@echo "it should be used with code in the build tree only.  To build an"
	@echo "installable version, use ./configure without the --enable-plugin-builddir"
	@echo "option.  Note that the autogen.sh script supplies the plugin builddir"
	@echo "option automatically - it cannot be used to configure installable builds."
	@echo
	@/bin/false
endif

