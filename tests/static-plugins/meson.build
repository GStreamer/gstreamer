
dep = dependency('gstreamer-full-1.0', required: get_option('default_library') == 'static')
if dep.found()
	test_elements = executable('test-gst-full', 'test-gst-full.c', dependencies : gst_full_dep)
	test('test-gst-full', test_elements)
endif
