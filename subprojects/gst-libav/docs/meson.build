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
    gst_plugins_doc_dep = custom_target('libav-plugins-doc-cache',
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


hotdoc_p = find_program('hotdoc', required: get_option('doc'))
if not hotdoc_p.found()
    message('Hotdoc not found, not building the documentation')
    subdir_done()
endif

hotdoc_req = '>= 0.11.0'
hotdoc_version = run_command(hotdoc_p, '--version', check: false).stdout()
if not hotdoc_version.version_compare(hotdoc_req)
    if get_option('doc').enabled()
        error('Hotdoc version @0@ not found, got @1@'.format(hotdoc_req, hotdoc_version))
    else
        message('Hotdoc version @0@ not found, got @1@'.format(hotdoc_req, hotdoc_version))
        subdir_done()
    endif
endif

build_hotdoc = true
hotdoc = import('hotdoc')
docconf = configuration_data()
docconf.set('GST_API_VERSION', api_version)

required_hotdoc_extensions = ['gst-extension']
foreach extension: required_hotdoc_extensions
    if not hotdoc.has_extensions(extension)
        if get_option('doc').enabled()
            error('Documentation enabled but gi-extension missing')
        endif

        message('@0@ extensions not found, not building documentation requiring it'.format(extension))
        subdir_done()
    endif
endforeach

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
    '--project_version', api_version,
    '--builddir', meson.current_build_dir(),
    '--sitemap', cdir / 'sitemap.txt',
    '--index', cdir / 'index.md',
    '--gst_index', cdir / 'index.md',
    '--gst_cache_file', '@INPUT@',
    '--output', '@OUTPUT@',
    '--gst_c_source_file', doc_source_file,
    '--gst_plugin_libraries_file', doc_plugin_libs_file,
  ],
  input: plugins_cache,
  output: 'hotdoc-configs.json',
)

