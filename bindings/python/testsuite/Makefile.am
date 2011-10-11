EXTRA_DIST = \
	common.py \
	test_timeline_pipeline.py \
	test_timeline.py \
	runtests.py \
	test_global_functions.py \
	test_timeline_test_source.py \
	test_layer.py \
	test_timeline_title_source.py \
	test_simple_layer.py \
	test_track.py \
	test_textoverlay.py \
	test_transition.py \
	test_timeline_file_source.py \
	test_timeline_parse_launch_effect.py

check:
	@PYTHONPATH=$(top_builddir):$(top_builddir)/bindings/python/.libs:`pwd`:$(top_srcdir):$(PYTHONPATH) $(PYTHON) $(srcdir)/runtests.py
	@rm -fr *.pyc
