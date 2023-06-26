gst_tester = executable('gst-tester-' + apiversion,
    'gst-tester.c',
    c_args : gst_c_args,
    include_directories : [configinc],
    install: true,
    install_tag: 'bin-devel',
    dependencies : [gio_dep],
)

meson.override_find_program('gst-tester-' + apiversion, gst_tester)

tests = [
    'simplest',
]

env = environment()
env.set('GST_PLUGIN_PATH_1_0', meson.global_build_root())
env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
env.set('GST_REGISTRY', '@0@/@1@.registry'.format(meson.current_build_dir(), 'validate'))
env.set('GST_PLUGIN_SCANNER_1_0', gst_scanner_dir + '/gst-plugin-scanner')
env.set('GST_PLUGIN_LOADING_WHITELIST', 'gstreamer', 'gst-validate', 'debugutilsbad')

foreach t: tests
    test_dir_name = t.split('/')
    test_name = 'validate'
    foreach c: test_dir_name
        test_name += '.' + c
    endforeach
    test_env = env
    test_env.set('GST_VALIDATE_LOGSDIR', join_paths(meson.current_build_dir(), test_name))
    test_file = join_paths(meson.current_source_dir(), t + '.validatetest')
    test(test_name, gst_tester, args: [test_file, '--use-fakesinks'],
        env: test_env, timeout : 3 * 60, protocol: 'tap')
endforeach
