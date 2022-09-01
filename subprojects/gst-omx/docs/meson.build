build_hotdoc = false

if meson.is_cross_build()
    if get_option('doc').enabled()
        error('Documentation enabled but building the doc while cross building is not supported yet.')
    endif

    message('Documentation not built as building the docmentation while cross building is not supported yet.')
    subdir_done()
endif

required_hotdoc_extensions = ['gst-extension']
if gst_dep.type_name() == 'internal'
    gst_proj = subproject('gstreamer')
    plugins_cache_generator = gst_proj.get_variable('plugins_cache_generator')
else
    required_hotdoc_extensions += ['gst-extension']
    plugins_cache_generator = find_program(join_paths(gst_dep.get_variable('libexecdir'), 'gstreamer-' + api_version, 'gst-plugins-doc-cache-generator'),
        required: false)
endif

plugins_cache = join_paths(meson.current_source_dir(), 'gst_plugins_cache.json')
if plugins_cache_generator.found()
    gst_plugins_doc_dep = custom_target('omx-plugins-doc-cache',
        command: [plugins_cache_generator, plugins_cache, '@OUTPUT@', '@INPUT@'],
        input: plugins,
        output: 'gst_plugins_cache.json',
    )
else
    warning('GStreamer plugin inspector for documentation not found, can\'t update the cache')
endif

hotdoc_p = find_program('hotdoc', required: get_option('doc'))
if not hotdoc_p.found()
    message('Hotdoc not found, not building the documentation')
    subdir_done()
endif

build_hotdoc = true
hotdoc = import('hotdoc')
if not hotdoc.has_extensions(required_hotdoc_extensions)
    if get_option('doc').enabled()
        error('Documentation enabled but gi-extension missing')
    endif

    message('@0@ extensions not found, not building documentation'.format(required_hotdoc_extensions))
    subdir_done()
endif

message('Plugins: @0@'.format(plugins))
libs_doc = []
plugins_doc = [hotdoc.generate_doc('omx',
    project_version: api_version,
    sitemap: 'sitemap.txt',
    index: 'index.md',
    gst_index: 'index.md',
    gst_smart_index: true,
    gst_c_sources: ['../gst/*/*.[ch]',],
    gst_cache_file: plugins_cache,
    gst_plugin_name: 'omx',
    dependencies: [gstomx],
)]
