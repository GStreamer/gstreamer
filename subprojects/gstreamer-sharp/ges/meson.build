pkg = 'gst-editing-services'
raw_api_fname = join_paths(meson.current_source_dir(), pkg + '-api.raw')
metadata  = files(pkg + '.metadata')

subdir('generated')

ges_sharp = shared_library(pkg + '-sharp', ges_generate_files,
        cs_args: ['-nowarn:169', '-nowarn:108', '-nowarn:114', '-unsafe'],
        link_with: gst_sharp,
        dependencies: [glib_sharp_dep, gio_sharp_dep])

ges_sharp_dep = declare_dependency(dependencies: [glib_sharp_dep, gio_sharp_dep, gst_sharp_dep], link_with: ges_sharp)

configure_file(
    input: pkg + '-sharp.dll.config',
    output: pkg + '-sharp.dll.config',
    copy: true)

if add_languages('c', required: get_option('tests')) and csc.get_id() == 'mono'
    c_abi_exe = executable(pkg + '_c_abi', c_abi,
            c_args: ['-Wno-deprecated', '-Wno-deprecated-declarations'],
            dependencies: [gst_deps, ges_dep])

    cs_abi_exe = executable(pkg + '_cs_abi', cs_abi,
            cs_args: ['-nowarn:169', '-nowarn:108', '-nowarn:114', '-nowarn:0618', '-unsafe'],
            dependencies: [ges_sharp_dep])

    test(pkg + 'abi', diff, args: [c_abi_exe.full_path(), cs_abi_exe.full_path()],
            env: testsenv)
else
    message('Not running tests ' + csc.get_id())
endif
