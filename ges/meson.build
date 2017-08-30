pkg = 'gst-editing-services'
raw_api_fname = join_paths(meson.current_source_dir(), pkg + '-api.raw')
metadata_fname = join_paths(meson.current_source_dir(), pkg + '.metadata')
snk = join_paths(meson.current_source_dir(), '../', meson.project_name() + '.snk')

glueincludes = 'ges/ges.h'
gluefile = join_paths(meson.current_build_dir(), 'generate.c')

sources = [
        't.cs'
]

subdir('generated')

ges_sharp = library(pkg + '-sharp', source_gen, sources,
        cs_args: ['-nowarn:169', '-nowarn:108', '-nowarn:114', '-unsafe',
            '-keyfile:' + snk],
        link_with: gst_sharp,
        dependencies: [glib_sharp_dep, gio_sharp_dep])

gluegen = custom_target(pkg + 'gluegen',
        input: raw_api_fname,
        output: 'generate.c',
        command: [generate_api, '--fakeglue'],
        depends: [source_gen])

library('gessharpglue-1.0.0', gluegen, dependencies: gst_deps + [ges_dep])

ges_sharp_dep = declare_dependency(dependencies: [glib_sharp_dep, gio_sharp_dep, gst_sharp_dep,
    ges_dep], link_with: ges_sharp)

configure_file(
    input: pkg + '-sharp.dll.config',
    output: pkg + '-sharp.dll.config',
    configuration: configuration_data())


