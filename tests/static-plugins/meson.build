
dep = dependency('gstreamer-full-1.0', required: get_option('default_library') == 'static')
if dep.found()
	test_gst_full_features = executable('test-gst-full-features', 'test-gst-full-features.c', dependencies : gst_full_dep, c_args: ['-DHAVE_CONFIG_H'])
	test('test-gst-full-features', test_gst_full_features)
	test_gst_full = executable('test-gst-full', 'test-gst-full.c', dependencies : gst_full_dep, c_args: ['-DHAVE_CONFIG_H'])
	test('test-gst-full', test_gst_full)
endif
