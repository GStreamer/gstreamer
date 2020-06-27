gstvalidate_sources = files(
    'gst-validate-reporter.c',
    'gst-validate-mockdecryptor.c',
    'gst-validate-monitor.c',
    'gst-validate-element-monitor.c',
    'gst-validate-bin-monitor.c',
    'gst-validate-pipeline-monitor.c',
    'gst-validate-pad-monitor.c',
    'gst-validate-monitor-factory.c',
    'gst-validate-report.c',
    'gst-validate-scenario.c',
    'gst-validate-override.c',
    'gst-validate-utils.c',
    'gst-validate-override-registry.c',
    'media-descriptor.c',
    'media-descriptor-writer.c',
    'media-descriptor-parser.c',
    'gst-validate-media-info.c',
    'gst-validate-extra-checks.c',
    'flow/gstvalidateflow.c',
    'flow/formatting.c',
    'validate.c',
)

gstvalidate_headers = files(
    'validate.h',
    'validate-prelude.h',
    'gst-validate-types.h',
    'gst-validate-bin-monitor.h',
    'gst-validate-pipeline-monitor.h',
    'gst-validate-element-monitor.h',
    'gst-validate-enums.h',
    'media-descriptor.h',
    'media-descriptor-writer.h',
    'media-descriptor-parser.h',
    'gst-validate-monitor-factory.h',
    'gst-validate-monitor.h',
    'gst-validate-override.h',
    'gst-validate-override-registry.h',
    'gst-validate-pad-monitor.h',
    'gst-validate-reporter.h',
    'gst-validate-report.h',
    'gst-validate-runner.h',
    'gst-validate-scenario.h',
    'gst-validate-utils.h',
    'gst-validate-media-info.h'
)

install_headers(gstvalidate_headers, subdir : 'gstreamer-1.0/gst/validate')

gst_validate_enums = gnome.mkenums('gstvalidateenumtypes',
  sources : gstvalidate_headers,
  h_template : 'gst-validate-enum-types.h.template',
  c_template : 'gst-validate-enum-types.c.template',
  install_header : true,
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/gst/validate'))

validate_deps = [gst_check_dep, gst_dep, gst_controller_dep, gstbase_dep, glib_dep, gio_dep, gmodule_dep, gst_pbutils_dep, mathlib, json_dep]
gstvalidate = library('gstvalidate-1.0',
    sources: files('gst-validate-runner.c') + gstvalidate_sources + gst_validate_enums,
    version : libversion,
    soversion : soversion,
    darwin_versions : osxversion,
    include_directories : [inc_dirs],
    install: true,
    c_args : [gst_c_args] + ['-D_GNU_SOURCE'],
    dependencies : validate_deps)

gstvalidatetracer = library('gstvalidatetracer',
    sources: files('gst-validate-runner.c') + gst_validate_enums,
    include_directories : [inc_dirs],
    install: true,
    c_args : [gst_c_args] + ['-D__GST_VALIDATE_PLUGIN', '-D_GNU_SOURCE'],
    install_dir : plugins_install_dir,
    objects: gstvalidate.extract_objects(gstvalidate_sources),
    dependencies : validate_deps)

plugins += gstvalidatetracer
pkgconfig.generate(gstvalidate,
  libraries : [gst_dep],
  subdirs : pkgconfig_subdirs,
  name : 'gst-validate-1.0',
  description : 'Gstreamer Validate',
)

validate_gen_sources = []
if build_gir
    gst_validate_gir_extra_args = gir_init_section + [ '--c-include=gst/validate/validate.h' ]
    validate_gir = gnome.generate_gir(gstvalidate,
            sources : gstvalidate_sources + gstvalidate_headers + gst_validate_enums,
            nsversion : '1.0',
            namespace : 'GstValidate',
            symbol_prefix : 'gst_validate',
            identifier_prefix : 'GstValidate',
            export_packages : 'gst-validate-' + apiversion,
            includes : ['GObject-2.0',
                        'GLib-2.0',
                        'Gio-2.0',
                        'GModule-2.0',
                        'Gst-' + apiversion,
                        'GstPbutils-' + apiversion],
            install : true,
            dependencies : validate_deps,
            extra_args : gst_validate_gir_extra_args,
    )
    validate_gen_sources += [validate_gir]
endif

validate_dep = declare_dependency(link_with : gstvalidate,
  include_directories : [inc_dirs],
  dependencies : validate_deps,
  sources : validate_gen_sources
)

meson.override_dependency('gst-validate-1.0', validate_dep)

pkgconfig.generate(gstvalidatetracer, install_dir : plugins_pkgconfig_install_dir)
