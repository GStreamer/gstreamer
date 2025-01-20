build_hotdoc = false

if get_option('doc').disabled()
  subdir_done()
endif

if meson.is_cross_build()
    if get_option('doc').enabled()
        error('Documentation enabled but building the doc while cross building is not supported yet.')
    endif

    message('Documentation not built as building it while cross building is not supported yet.')
    subdir_done()
endif

if static_build
    if get_option('doc').enabled()
        error('Documentation enabled but not supported when building statically.')
    endif

    message('Building statically, can\'t build the documentation')
    subdir_done()
endif

if not build_gir
    if get_option('doc').enabled()
        error('Documentation enabled but introspection not built.')
    endif

    message('Introspection not built, won\'t build documentation requiring it')
    subdir_done()
endif

if gst_dep.type_name() == 'internal'
    gst_proj = subproject('gstreamer')
    plugins_cache_generator = gst_proj.get_variable('plugins_cache_generator')
else
    plugins_cache_generator = find_program('gst-plugins-doc-cache-generator',
        dirs: [join_paths(gst_dep.get_variable('libexecdir', default_value: ''), 'gstreamer-' + api_version)],
        required: false)
endif

excludes = ['gettext.h',
    'gst-validate-internal.h',
    'gst-validate-i18n-lib.c'
]

build_hotdoc = true
validate_excludes = []
foreach f: excludes
    validate_excludes += [join_paths(meson.current_source_dir(), '..',
        'validate', 'gst', 'validate', f)]
endforeach

cdir = meson.current_source_dir()
doc_sources = []
foreach s: gstvalidate_headers + gstvalidate_sources
  doc_sources += s.full_path()
endforeach

lib_sources = {
  'validate': pathsep.join(doc_sources)
}

lib_doc_source_file = configure_file(
  output: 'lib_doc_sources.json',
  configuration: lib_sources,
  output_format: 'json')

lib_doc_gi_source_file = configure_file(
  output: 'lib_doc_gi_sources.json',
  configuration: {'validate': validate_gir[0].full_path()},
  output_format: 'json')

lib_hotdoc_config = custom_target(
  'build-gst-hotdoc-configs',
  command: [
    plugins_cache_generator,
    'hotdoc-lib-config',
    '--srcdir', cdir,
    '--builddir', meson.current_build_dir(),
    '--buildroot', meson.global_build_root(),
    '--project_version', api_version,
    '--gi_source_file', lib_doc_gi_source_file,
    '--gi_c_source_file', lib_doc_source_file,
    '--gi_c_source_filters', validate_excludes,
    '--source_root', cdir,
    '--gi_source_root', cdir / '..' / 'validate' / 'gst' / 'validate',
    '--output', '@OUTPUT@',
  ],
  output: 'hotdoc-lib-configs.json',
  depends: [validate_gir[0]],
)
