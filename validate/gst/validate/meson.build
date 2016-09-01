gstvalidate_sources = [
    'gst-validate-runner.c',
    'gst-validate-reporter.c',
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
    'validate.c',
    'validate.h',
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
    'gst-validate-media-info.h']

gstvalidate = shared_library('gstvalidate',
    sources: gstvalidate_sources,
    version : libversion,
    soversion : soversion,
    include_directories : [inc_dirs],
    install: true,
    c_args : [gst_c_args],
    dependencies : [gst_dep, glib_dep, gio_dep, gmodule_dep,
                    gst_pbutils_dep, mathlib, json_dep])

validate_gen_sources = []
if build_gir
    gst_validate_gir_extra_args = gir_init_section + [ '--c-include=gst/validate/validate.h' ]
    if meson.is_subproject()
      # FIXME: There must be a better way to do this
      # Need to pass the include path to find gst/gst.h and gst/gstenumtypes.h (built)
      gst_validate_gir_extra_args += ['--cflags-begin',
	  '-I' + meson.current_source_dir() + '/../../',
	  '-I' + meson.current_build_dir() + '/../../',
	  '--cflags-end']
    endif
    validate_gen_sources = [gnome.generate_gir(gstvalidate,
            sources : gstvalidate_sources,
            nsversion : '1.0',
            namespace : 'GstValidate',
            symbol_prefix : 'gst_',
            identifier_prefix : 'Gst',
            export_packages : 'gst-validate-' + apiversion,
            includes : ['GObject-2.0',
                        'GLib-2.0',
                        'Gio-2.0',
                        'GModule-2.0',
                        'GstVideo-' + apiversion,
                        'Gst-' + apiversion,
                        'GstPbutils-' + apiversion],
            install : true,
            dependencies : [gst_dep, glib_dep, gio_dep, gst_pbutils_dep],
    )]
endif

validate_dep = declare_dependency(link_with : gstvalidate,
  include_directories : [inc_dirs],
  dependencies : [gst_dep, glib_dep, gio_dep, gmodule_dep,
                  gst_pbutils_dep, mathlib],
  sources : validate_gen_sources
)
