tests =  \
	caps.py \
	common.py \
	element.py \
	interface.py \
	pipeline.py

check-local:
	@PYTHONPATH=$(top_builddir) $(PYTHON) $(srcdir)/runtests.py
	@rm -fr *.pyc

EXTRA_DIST = $(tests) runtests.py
