# rule to download the latest .po files
download-po: $(top_srcdir)/common/download-translations
	$(top_srcdir)/common/download-translations $(PACKAGE)

