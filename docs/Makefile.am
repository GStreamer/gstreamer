EXTRA_DIST = HEAD.in docutils.conf gst-python.css

DOCS = \
	$(srcdir)/HEAD \
	$(top_srcdir)/NEWS \
	$(top_srcdir)/README \
	$(top_srcdir)/TODO \
	$(top_srcdir)/AUTHORS

gst-python.html: docutils.conf ${DOCS}
	@if [ ! $$DOCUTILSHOME ]; then \
		echo "ERROR: Please set \$$DOCUTILSHOME"; \
		exit 1; \
	fi
	@if [ ! -r $$DOCUTILSHOME/tools/html.py ]; then \
		echo "\$$DOCUTILSHOME/tools/html.py not found"; \
		exit 1; \
	fi
	rm -f gst-python.txt
	for doc in ${DOCS}; do \
		cat $$doc >> gst-python.txt; \
		printf "\n" >> gst-python.txt; \
	done
	PYTHONPATH=$$PYTHONPATH:$$DOCUTILSHOME $$DOCUTILSHOME/tools/html.py gst-python.txt gst-python.html
	lynx -dump -force_html gst-python.html > gst-python.out

CLEANFILES = gst-python.txt gst-python.html
