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

hotdoc = import('hotdoc')
required_hotdoc_extensions = ['gi-extension', 'gst-extension']
foreach extension: required_hotdoc_extensions
    if not hotdoc.has_extensions(extension)
        if get_option('doc').enabled()
            error('Documentation enabled but @0@ missing'.format(extension))
        endif

        message('@0@ extension not found, not building documentation'.format(extension))
        subdir_done()
    endif
endforeach

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
docconf.set('GST_API_VERSION', apiversion)

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

libs_doc = [hotdoc.generate_doc('gstreamer',
    project_version: apiversion,
    gi_c_sources: '../gst/*.[hc]',
    gi_sources: [gst_gir[0].full_path()],
    gi_c_source_filters: gst_excludes,
    sitemap: 'gst/sitemap.txt',
    index: 'gst/index.md',
    gi_index: 'gst/gi-index.md',
    gi_smart_index: true,
    gi_c_source_roots: [join_paths(meson.current_source_dir(), '../gst/'), ],
    dependencies: [gst_dep, gmodule_dep],
    extra_assets: [join_paths(meson.current_source_dir(), 'images')],
    depends: gst_gir[0],
)]

libs = [
  ['base', gst_base_gir, gst_base_dep],
  ['controller', gst_controller_gir, gst_controller_dep,],
  ['net', gst_net_gir, gst_net_dep],
  ['check', gst_check_gir, gst_check_dep],
]

foreach lib: libs
  name = lib[0]
  gir = lib[1]
  deps = [lib[2], gir]
  libs_doc += [hotdoc.generate_doc(name,
      project_version: apiversion,
      gi_c_sources: ['../libs/gst/' + name + '/*.[hc]'],
      gi_c_source_filters: gst_excludes,
      gi_sources: gir[0].full_path(),
      gi_c_source_roots: [join_paths(meson.current_source_dir(), '../libs/gst/' + name), ],
      sitemap: join_paths('libs', name, 'sitemap.txt'),
      index: join_paths('libs/', name, 'index.md'),
      gi_index: join_paths('libs/', name, 'index.md'),
      gi_smart_index: true,
      gi_order_generated_subpages: true,
      dependencies: deps,
      install: false,
      depends: gir[0],
  )]
endforeach

cdir = meson.current_source_dir()
if host_machine.system() == 'windows'
  pathsep = ';'
else
  pathsep = ':'
endif
gst_plugins_doc = run_command(
    plugins_cache_generator,
    'hotdoc-config',
    '--builddir', meson.current_build_dir(),
    '--project_version', apiversion,
    '--sitemap', cdir / 'plugins/sitemap.txt',
    '--index', cdir / 'plugins/index.md',
    '--gst_index', cdir / 'plugins/index.md',
    '--gst_c_sources',
        cdir / '../plugins/*/*.c',
        cdir / '../plugins/*/*.h',
    '--gst_cache_file', plugins_cache,
    check: true,
).stdout().split(pathsep)
