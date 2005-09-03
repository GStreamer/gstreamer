# this file adds rules for installing html subtrees
# I really don't like this hack, but automake doesn't seem to want to
# install directory trees :(

if DOC_HTML
install-data-local: html
	$(mkinstalldirs) $(DESTDIR)$(docdir)/$(DOC)
	cp -pr $(HTML_DAT) $(DESTDIR)$(docdir)/$(DOC)

uninstall-local:
	for part in $(HTML_DAT); do rm -rf $(DESTDIR)$(docdir)/$(DOC)/$$part; done
else
install-data-local:
uninstall-local:
endif
