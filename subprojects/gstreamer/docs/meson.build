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

hotdoc_plugin_scanner = executable('gst-hotdoc-plugins-scanner',
  'gst-hotdoc-plugins-scanner.c',
  c_args : gst_c_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gmodule_dep, gio_dep],
  install_dir : helpers_install_dir,
  link_with: [printf_lib],
  install: true,
)

configure_file(
    input: 'gst-plugins-doc-cache-generator.py',
    output: 'gst-plugins-doc-cache-generator',
    install_dir: helpers_install_dir,
    copy: true
)

plugins_cache_generator = find_program(join_paths(meson.current_build_dir(), 'gst-plugins-doc-cache-generator'))
plugins_cache = join_paths(meson.current_source_dir(), 'plugins', 'gst_plugins_cache.json')
gst_plugins_doc_dep = custom_target('build-doc-cache',
    command: [plugins_cache_generator, plugins_cache, '@OUTPUT@', '@INPUT@'],
    input: plugins,
    output: 'gst_plugins_cache.json',
    depends: [hotdoc_plugin_scanner],
    build_always_stale: true,
)

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

    message('Introspection not built, can\'t build the documentation')
    subdir_done()
endif

all_plugins_paths = []
foreach l: plugins
    all_plugins_paths += l.full_path()
endforeach

build_hotdoc = true
docconf = configuration_data()
docconf.set('GST_API_VERSION', api_version)

version_entities = configure_file(input : 'version.in',
    output : 'gst_api_version.md',
    configuration : docconf)

gst_excludes = []
foreach h: ['gettext.h', 'glib-compat-private.h', 'glib-compat.h',
            'gst_private.h',
            'gstelementdetails.h', 'gstmacros.h', 'gstmarshal.h',
            'math-compat.h', 'parse/grammar.tab.h',
            '../libs/gst/base/gstindex.h',
            '../libs/gst/base/gstindex.c',
            '../libs/gst/check/internal-check.h',
            '../libs/gst/check/gstharnesslink.h',
            'parser/grammar.tab.pre.h', 'parse/parse_lex.h', 'types.h',
            'gst-printf.h', 'printf-args.h', 'printf-extension.h',
            'printf-parse.h', 'vasnprintf.h', 'gstregistrybinary.c',
            'gstregistrybinary.h', 'gstpluginloader.h', 'gstpluginloader.c']
    gst_excludes += [join_paths(meson.current_source_dir(), '..', 'gst', h)]
endforeach

cdir = meson.current_source_dir()

doc_sources = []
foreach s: gst_sources + gst_headers
  doc_sources += s.full_path()
endforeach

lib_sources = {
  'gst': pathsep.join(doc_sources)
}

gst_doc_source_file = configure_file(
  output: 'gst_doc_sources.json',
  configuration: lib_sources,
  output_format: 'json')

gst_doc_gi_source_file = configure_file(
  output: 'gst_doc_gi_sources.json',
  configuration: {'gst': gst_gir[0].full_path()},
  output_format: 'json'
)

lib_hotdoc_config = custom_target(
  'build-gst-hotdoc-configs',
  command: [
    plugins_cache_generator,
    'hotdoc-lib-config',
    '--srcdir', cdir,
    '--builddir', meson.current_build_dir(),
    '--buildroot', meson.global_build_root(),
    '--project_version', api_version,
    '--gi_source_file', gst_doc_gi_source_file,
    '--gi_c_source_file', gst_doc_source_file,
    '--gi_c_source_filters', gst_excludes,
    '--source_root', cdir / 'gst',
    '--gi_source_root', cdir / '..' / 'gst',
    '--output', '@OUTPUT@',
  ],
  output: 'hotdoc-gst-configs.json',
  depends: [gst_gir[0]],
)

libs_doc_source_file = configure_file(
  output: 'libs_doc_sources.json',
  configuration: libs_sources,
  output_format: 'json')

libs_doc_gi_source_file = configure_file(
  output: 'libs_doc_gi_sources.json',
  configuration: {
    'base': gst_base_gir[0].full_path(),
    'controller': gst_controller_gir[0].full_path(),
    'net': gst_net_gir[0].full_path(),
    'check': gst_check_gir[0].full_path(),
  },
  output_format: 'json')

libs_hotdoc_configs = custom_target(
  'build-libs-hotdoc-configs',
  command: [
    plugins_cache_generator,
    'hotdoc-lib-config',
    '--srcdir', cdir,
    '--builddir', meson.current_build_dir(),
    '--buildroot', meson.global_build_root(),
    '--project_version', api_version,
    '--gi_source_file', libs_doc_gi_source_file,
    '--gi_c_source_file', libs_doc_source_file,
    '--gi_c_source_filters', gst_excludes,
    '--source_root', cdir / 'libs',
    '--gi_source_root', cdir / '..' / 'libs' / 'gst',
    '--output', '@OUTPUT@',
  ],
  output: 'hotdoc-libs-configs.json',
  depends: [gst_base_gir[0], gst_controller_gir[0], gst_net_gir[0], gst_check_gir[0]],
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
    '--gst_c_source_file', doc_source_file,
    '--gst_plugin_libraries_file', doc_plugin_libs_file,
    '--gst_cache_file', '@INPUT@',
    '--output', '@OUTPUT@',
  ],
  input: plugins_cache,
  output: 'hotdoc-configs.json',
  depends: [hotdoc_plugin_scanner],
)
