# this file adds rules for installing html subtrees
# I really don't like this hack, but automake doesn't seem to want to
# install directory trees :(

if DOC_HTML
install-data-local: html
	$(mkinstalldirs) $(DESTDIR)$(docdir)
	cp -pr $(HTML_DAT) $(DESTDIR)$(docdir)

uninstall-local:
	for part in $(HTML_DAT); do rm -rf $(DESTDIR)$(docdir)/$$part; done
	rmdir $(DESTDIR)$(docdir)
else
install-data-local:
uninstall-local:
endif
