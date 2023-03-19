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

required_hotdoc_extensions = ['gst-extension']
if gst_dep.type_name() == 'internal'
    gst_proj = subproject('gstreamer')
    plugins_cache_generator = gst_proj.get_variable('plugins_cache_generator')
else
    plugins_cache_generator = find_program(join_paths(gst_dep.get_variable('libexecdir'), 'gstreamer-' + api_version, 'gst-plugins-doc-cache-generator'),
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

foreach extension: required_hotdoc_extensions
    if not hotdoc.has_extensions(extension)
        if get_option('doc').enabled()
            error('Documentation enabled but gi-extension missing')
        endif

        message('@0@ extensions not found, not building documentation requiring it'.format(extension))
        subdir_done()
    endif
endforeach


libs_doc = []
cdir = meson.current_source_dir()
if host_machine.system() == 'windows'
  pathsep = ';'
else
  pathsep = ':'
endif
gst_plugins_doc = run_command(
    plugins_cache_generator,
    'hotdoc-config',
    '--project_version', api_version,
    '--builddir', meson.current_build_dir(),
    '--sitemap', cdir / 'sitemap.txt',
    '--index', cdir / 'index.md',
    '--gst_index', cdir / 'index.md',
    '--gst_c_sources', cdir / '../ext/*/*.[ch]',
    '--gst_cache_file', plugins_cache,
    check: true,
).stdout().split(pathsep)

