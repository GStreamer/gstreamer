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

required_hotdoc_extensions = ['gi-extension', 'gst-extension']
if gst_dep.type_name() == 'internal'
    gst_proj = subproject('gstreamer')
    plugins_cache_generator = gst_proj.get_variable('plugins_cache_generator')
else
    plugins_cache_generator = find_program(join_paths(gst_dep.get_variable('libexecdir'), 'gstreamer-' + apiversion, 'gst-plugins-doc-cache-generator'), required: false)
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
        message('Hotdoc version @0@ not found, got @1@, not building documentation'.format(hotdoc_req, hotdoc_version))
        subdir_done()
    endif
endif

hotdoc = import('hotdoc')
foreach extension: required_hotdoc_extensions
    if not hotdoc.has_extensions(extension)
        if get_option('doc').enabled()
            error('Documentation enabled but gi-extension missing')
        endif

        message('@0@ extensions not found, not building documentation requiring it'.format(extension))
    endif
endforeach

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

hotdoc = import('hotdoc')
libs_doc = [hotdoc.generate_doc('gst-editing-services',
    project_version: apiversion,
    extra_assets: [join_paths(meson.current_source_dir(), 'images')],
    gi_c_sources: ges_sources + ges_headers,
    gi_c_source_roots: [join_paths(meson.current_source_dir(), '../ges/')],
    gi_sources: [ges_gir[0].full_path()],
    gi_c_source_filters: ges_excludes,
    sitemap: 'sitemap.txt',
    index: 'index.md',
    gi_index: 'index.md',
    gi_smart_index: true,
    gi_order_generated_subpages: true,
    dependencies: [ges_dep],
    disable_incremental_build: true,
    depends: ges_gir[0],
)]

if host_machine.system() == 'windows'
  pathsep = ';'
else
  pathsep = ':'
endif
cdir = meson.current_source_dir()
gst_plugins_doc = run_command(
    plugins_cache_generator,
    'hotdoc-config',
    '--builddir', meson.current_build_dir(),
    '--project_version', apiversion,
    '--sitemap', cdir / 'plugins/sitemap.txt',
    '--index', cdir / 'plugins/index.md',
    '--gst_index', cdir / 'plugins/index.md',
    '--gst_c_sources', cdir / '../plugins/*/*.[ch]',
    '--gst_cache_file', plugins_cache,
    check: true,
).stdout().split(pathsep)
