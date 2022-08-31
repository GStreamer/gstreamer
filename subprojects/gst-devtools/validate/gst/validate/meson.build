runner_file = files('gst-validate-runner.c')
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

validate_deps = [gst_check_dep, gst_dep, gst_controller_dep, gstbase_dep,
                 gio_dep, gmodule_dep, gst_pbutils_dep, mathlib, json_dep]
gstvalidate = library('gstvalidate-@0@'.format(apiversion),
    sources: gstvalidate_sources + gst_validate_enums + runner_file,
    version : libversion,
    soversion : soversion,
    darwin_versions : osxversion,
    include_directories : [inc_dirs],
    install: true,
    c_args : [gst_c_args] + ['-D_GNU_SOURCE', '-DBUILDING_GST_VALIDATE'],
    dependencies : validate_deps)

if not static_build
  gstvalidatetracer = library('gstvalidatetracer',
      sources: gst_validate_enums + runner_file,
      include_directories : [inc_dirs],
      install: true,
      c_args : [gst_c_args] + ['-D__GST_VALIDATE_PLUGIN', '-D_GNU_SOURCE', '-DBUILDING_GST_VALIDATE'],
      install_dir : plugins_install_dir,
      objects: gstvalidate.extract_objects(gstvalidate_sources),
      dependencies : validate_deps)
  plugins += gstvalidatetracer

else
  warning('Statically building GstValidate as a tracer is not supported yet.')
endif


pkg_name = 'gstreamer-validate-1.0'
library_def = {'lib': gstvalidate}
pkgconfig.generate(gstvalidate,
  libraries : [gst_dep],
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'GStreamer Validate',
)

validate_gen_sources = []
if build_gir
    gst_validate_gir_extra_args = gir_init_section + [ '--c-include=gst/validate/validate.h' ]
    gir = {
            'sources' : runner_file + gstvalidate_sources + gstvalidate_headers + gst_validate_enums,
            'nsversion' : '1.0',
            'namespace' : 'GstValidate',
            'symbol_prefix' : 'gst_validate',
            'identifier_prefix' : 'GstValidate',
            'export_packages' : pkg_name,
            'includes' : ['GObject-2.0',
                        'GLib-2.0',
                        'Gio-2.0',
                        'GModule-2.0',
                        'Gst-' + apiversion,
                        'GstPbutils-' + apiversion],
            'install' : true,
            'dependencies' : validate_deps,
            'extra_args' : gst_validate_gir_extra_args,
    }
    library_def += {'gir': [gir]}

    if not static_build
      validate_gir = gnome.generate_gir(gstvalidate, kwargs: gir)
      library_def += {'gir_targets':  library_def.get('gir_targets', []) + [validate_gir]}
      validate_gen_sources += [validate_gir]
    endif
endif
gst_libraries += [[pkg_name, library_def]]

validate_dep = declare_dependency(link_with : gstvalidate,
  include_directories : [inc_dirs],
  dependencies : validate_deps,
  sources : validate_gen_sources
)

meson.override_dependency(pkg_name, validate_dep)

