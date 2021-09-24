build_hotdoc = false

required_hotdoc_extensions = ['gst-extension']
plugins_cache = join_paths(meson.current_source_dir(), 'gst_plugins_cache.json')
if gst_dep.type_name() == 'internal'
    gst_proj = subproject('gstreamer')
    plugins_cache_generator = gst_proj.get_variable('plugins_cache_generator')
else
    plugins_cache_generator = find_program(join_paths(gst_dep.get_pkgconfig_variable('libexecdir'), 'gstreamer-' + api_version, 'gst-plugins-doc-cache-generator'),
        required: false)
endif

if plugins.length() == 0
    message('All ugly plugins have been disabled')
elif plugins_cache_generator.found()
    plugins_doc_dep = custom_target('ugly-plugins-doc-cache',
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
hotdoc_version = run_command(hotdoc_p, '--version').stdout()
if not hotdoc_version.version_compare(hotdoc_req)
    if get_option('doc').enabled()
        error('Hotdoc version @0@ not found, got @1@'.format(hotdoc_req, hotdoc_version))
    else
        message('Hotdoc version @0@ not found, got @1@'.format(hotdoc_req, hotdoc_version))
        subdir_done()
    endif
endif

hotdoc = import('hotdoc')
foreach extension: required_hotdoc_extensions
    if not hotdoc.has_extensions(extension)
        if get_option('doc').enabled()
            error('Documentation enabled but @0@ missing'.format(extension))
        endif

        message('@0@ extension not found, not building documentation'.format(extension))
        subdir_done()
    endif
endforeach

build_hotdoc = true
docconf = configuration_data()
docconf.set('GST_API_VERSION', api_version)

configure_file(input : 'gst_api_version.in',
               output : 'gst_api_version.md',
               configuration : docconf)

root_rel = '..'
excludes = []
foreach f: []
    excludes += [join_paths(meson.current_source_dir(), root_rel, '..', f)]
endforeach

libs_doc = []
plugins_doc = []
list_plugin_res = run_command(python3, '-c',
'''
import sys
import json

with open("@0@") as f:
        print(':'.join(json.load(f).keys()), end='')
'''.format(plugins_cache))

assert(list_plugin_res.returncode() == 0,
  'Could not list plugins from @0@'.format(plugins_cache))

foreach plugin_name: list_plugin_res.stdout().split(':')
    plugins_doc += [hotdoc.generate_doc(plugin_name,
        project_version: api_version,
        sitemap: 'sitemap.txt',
        index: 'index.md',
        gst_index: 'index.md',
        gst_smart_index: true,
        gst_c_sources: [
            join_paths(root_rel, 'sys/*/*.[ch]'),
            join_paths(root_rel, 'ext/*/*.[ch]'),
            join_paths(root_rel, 'gst/*/*.[ch]'),
        ],
        gst_c_source_filters: excludes,
        dependencies: [gst_dep, plugins],
        gst_order_generated_subpages: true,
        disable_incremental_build: true,
        gst_cache_file: plugins_cache,
        gst_plugin_name: plugin_name,
    )]
endforeach
