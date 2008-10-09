# run lcov from scratch, always
lcov-reset:
	make lcov-run
	make lcov-report

# run lcov from scratch if the dir is not there
lcov:
	make lcov-reset

# reset run coverage tests
lcov-run:
	@-rm -rf lcov
	find . -name "*.gcda" -exec rm {} \;
	if test -d tests/check; then make -C tests/check inspect; fi
	make check

# generate report based on current coverage data
lcov-report:
	mkdir lcov
	lcov --directory . --capture --output-file lcov/lcov.info
	lcov -l lcov/lcov.info | grep -v "`cd $(top_srcdir) && pwd`" | cut -d: -f1 > lcov/remove
	lcov -l lcov/lcov.info | grep "tests/check/" | cut -d: -f1 >> lcov/remove
	lcov -r lcov/lcov.info `cat lcov/remove` > lcov/lcov.cleaned.info
	rm lcov/remove
	mv lcov/lcov.cleaned.info lcov/lcov.info
	genhtml -t "$(PACKAGE_STRING)" -o lcov lcov/lcov.info

lcov-upload: lcov
	rsync -rvz -e ssh --delete lcov/* gstreamer.freedesktop.org:/srv/gstreamer.freedesktop.org/www/data/coverage/lcov/$(PACKAGE)
