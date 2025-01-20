build_hotdoc = false

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

plugins_cache = join_paths(meson.current_source_dir(), 'gst_plugins_cache.json')

if plugins_cache_generator.found()
    gst_plugins_doc_dep = custom_target('editing-services-doc-cache',
        command: [plugins_cache_generator, plugins_cache, '@OUTPUT@', '@INPUT@'],
        input: plugins,
        output: 'gst_plugins_cache.json',
        build_always_stale: true,
    )
else
    warning('GStreamer plugin inspector for documentation not found, can\'t update the cache')
endif

if get_option('doc').disabled()
  subdir_done()
endif

build_hotdoc = true
ges_excludes = []
foreach f: ['gesmarshal.*',
            'ges-internal.*',
            'ges-auto-transition.*',
            'ges-structured-interface.*',
            'ges-structure-parser.*',
            'ges-version.h',
            'ges-smart-*',
            'ges-command-line-formatter.*',
            'ges-base-xml-formatter.h',
            'gstframepositioner.*',
            'lex.priv_ges_parse_yy.c',
            'ges-parse-lex.[c]']
    ges_excludes += [join_paths(meson.current_source_dir(), '..', '..', 'ges', f)]
endforeach

cdir = meson.current_source_dir()
doc_sources = []
foreach s: ges_headers + ges_sources
  doc_sources += s.full_path()
endforeach

lib_sources = {
  'ges': pathsep.join(doc_sources)
}

lib_doc_source_file = configure_file(
  output: 'lib_doc_sources.json',
  configuration: lib_sources,
  output_format: 'json')

lib_doc_gi_source_file = configure_file(
  output: 'lib_doc_gi_sources.json',
  configuration: {'ges': ges_gir[0].full_path()},
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
    '--gi_c_source_filters', ges_excludes,
    '--source_root', cdir,
    '--gi_source_root', cdir / '..' / 'ges',
    '--output', '@OUTPUT@',
  ],
  output: 'hotdoc-lib-configs.json',
  depends: [ges_gir[0]]
)

doc_source_file = configure_file(output: 'doc_sources.json', configuration: plugin_sources, output_format: 'json')

plugin_libraries = {}

foreach plugin: plugins
  if plugin.name().startswith('gst')
    plugin_name = plugin.name().substring(3)
  else
    plugin_name = plugin.name()
  endif

  plugin_libraries += {
    plugin_name: plugin.full_path()
  }
endforeach

doc_plugin_libs_file = configure_file(output: 'doc_plugin_libs.json', configuration: plugin_libraries, output_format: 'json')

cdir = meson.current_source_dir()
plugin_hotdoc_configs = custom_target(
  'build-hotdoc-configs',
  command: [
    plugins_cache_generator,
    'hotdoc-config',
    '--builddir', meson.current_build_dir(),
    '--project_version', api_version,
    '--sitemap', cdir / 'plugins/sitemap.txt',
    '--index', cdir / 'plugins/index.md',
    '--gst_index', cdir / 'plugins/index.md',
    '--gst_cache_file', '@INPUT@',
    '--output', '@OUTPUT@',
    '--gst_c_source_file', doc_source_file,
    '--gst_plugin_libraries_file', doc_plugin_libs_file,
  ],
  input: plugins_cache,
  output: 'hotdoc-configs.json',
)
