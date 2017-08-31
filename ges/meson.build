pkg = 'gst-editing-services'
raw_api_fname = join_paths(meson.current_source_dir(), pkg + '-api.raw')
metadata_fname = join_paths(meson.current_source_dir(), pkg + '.metadata')
snk = join_paths(meson.current_source_dir(), '../', meson.project_name() + '.snk')

subdir('generated')

ges_sharp = library(pkg + '-sharp', source_gen,
        cs_args: ['-nowarn:169', '-nowarn:108', '-nowarn:114', '-unsafe',
            '-keyfile:' + snk],
        link_with: gst_sharp,
        dependencies: [glib_sharp_dep, gio_sharp_dep])

ges_sharp_dep = declare_dependency(dependencies: [glib_sharp_dep, gio_sharp_dep, gst_sharp_dep,
    ges_dep], link_with: ges_sharp)

configure_file(
    input: pkg + '-sharp.dll.config',
    output: pkg + '-sharp.dll.config',
    configuration: configuration_data())


