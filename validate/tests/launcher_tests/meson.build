launcher = find_program(meson.build_root() + '/validate/tools/gst-validate-launcher',
    required : false)

if launcher.found()
    test_name = 'launcher_tests'
    test(test_name, launcher, args: ['-o', meson.build_root() + '/validate-launcher-output/',
      meson.current_source_dir() + '/test_validate.py'],
      env: ['GST_REGISTRY=@0@/@1@.registry'.format(meson.current_build_dir(), test_name)])
endif
