project('GStreamer manuals and tutorials', 'c',
  version: '1.23.0.1',
  meson_version : '>= 0.62')

hotdoc_p = find_program('hotdoc')
if not hotdoc_p.found()
    message('Hotdoc not found, not building the documentation')
    subdir_done()
endif

hotdoc_req = '>= 0.12.2'
hotdoc_version = run_command(hotdoc_p, '--version', check: false).stdout()
if not hotdoc_version.version_compare(hotdoc_req)
    error('Hotdoc version @0@ not found, got @1@'.format(hotdoc_req, hotdoc_version))
endif

hotdoc = import('hotdoc')
hotdoc_subprojects = []

apiversion = '1.0'
if host_machine.system() == 'windows'
  pathsep = ';'
else
  pathsep = ':'
endif

python3 = import('python').find_installation()

built_subprojects = get_option('built_subprojects')
if built_subprojects != ''
  message('Have subprojects list from options')
else
  read_file_contents = '''
import os
import sys

assert len(sys.argv) >= 2
fname = sys.argv[1]

with open(fname, 'r') as f:
    for l in f:
        print(l)
'''

  # gst-build will generate this file for us to consume so that subproject
  # changes can still work
  fname = join_paths(meson.project_build_root(), '..', '..', 'GstDocumentedSubprojects')
  cmdres = run_command(
      python3,
      '-c', read_file_contents,
      fname,
      check: false,
    )
  if cmdres.returncode() == 0
    built_subprojects = cmdres.stdout().strip()
    message('Have subprojects from file: @0@'.format(fname))
  endif
endif

libs = ''
plugins_doc = ''
deps = []
plugins_sitemap = ''
if built_subprojects != ''
    foreach project_name: built_subprojects.split(',')
        sub = subproject(project_name)
        if sub.get_variable('build_hotdoc')
            message('Building @0@ documentation'.format(project_name))

            foreach lib: sub.get_variable('libs_doc', [])
              hotdoc_subprojects += [lib]
              libs += lib.full_path() + pathsep
              deps += [lib]
            endforeach

            foreach plugin_doc: sub.get_variable('plugins_doc', [])
              warning(project_name+ ': variable plugins_doc is deprecated, use gst_plugins_doc instead')
              plugins_doc += plugin_doc.full_path() + pathsep
              hotdoc_subprojects += [plugin_doc]
              deps += [plugin_doc]
            endforeach

            foreach plugin_doc: sub.get_variable('gst_plugins_doc', [])
              plugins_doc += plugin_doc + pathsep
            endforeach
        else
            message('@0@ did not build hotdoc documentation, can\'t build API doc'.format(project_name))
        endif
    endforeach
endif
deps += [dependency('gstreamer-' + apiversion, fallback: ['gstreamer', 'gst_dep'])]

if get_option('use_portal_index')
  index = 'markdown/index.md'
else
  index = 'markdown/simple-index.md'
endif
sitemap_gen = find_program('scripts/generate_sitemap.py')
sitemap = configure_file(command: [sitemap_gen, '@INPUT@', '@OUTPUT@',
  'gi-index', libs, plugins_doc],
  input: 'sitemap.txt',
  output: 'sitemap.txt')

html_theme = 'https://github.com/hotdoc/hotdoc_lumen_theme/releases/download/0.15/hotdoc_lumen_theme-0.15.tar.xz?sha256=1ea7fd00d8dfd607d175c59e2d89918a55114c867f697a94c703f314b0d9b4b6'

gstreamer_doc = hotdoc.generate_doc('GStreamer',
    project_version: apiversion,
    sitemap: sitemap,
    index: index,
    gi_index: index,
    install: true,
    extra_assets: [join_paths(meson.current_source_dir(), 'images')],
    syntax_highlighting_activate: true,
    html_theme: html_theme,
    include_paths: [
      meson.current_source_dir() / 'examples',
      meson.current_source_dir() / 'markdown' / 'tutorials' / 'basic',
      meson.current_source_dir() / 'markdown' / 'templates',
      meson.current_source_dir() / 'markdown' / 'tutorials' / 'playback',
    ],
    html_extra_theme: join_paths(meson.current_source_dir(), 'theme/extra'),
    dependencies: deps,
    subprojects: hotdoc_subprojects,
    disable_incremental_build: true,
    gst_list_plugins_page: 'gst-index',
    gst_index: join_paths(meson.current_source_dir(), 'markdown/plugins_doc.md'),
    devhelp_activate: true,
    devhelp_online: 'https://gstreamer.freedesktop.org/documentation/',
    build_always_stale: true,
    edit_on_github_repository: 'https://gitlab.freedesktop.org/gstreamer/gst-docs/',
    previous_symbol_index: join_paths(meson.current_source_dir(), 'symbols', 'symbol_index.json'),
    fatal_warnings: get_option('fatal_warnings')
)

# For devhelp
meson.add_devenv({'XDG_DATA_DIRS': meson.current_build_dir() / 'GStreamer-doc'},
  method: 'prepend')

cdata = configuration_data()
cdata.set('GST_API_VERSION', apiversion)
readme = configure_file(input: 'scripts/RELEASE_README.md',
               output: 'README.md',
               configuration : cdata)

run_target('release',
  command: [find_program('scripts/release.py'),
            gstreamer_doc.full_path(),
            meson.project_version(),
            meson.current_build_dir()],
  depends: [gstreamer_doc]
)

meson.add_dist_script('scripts/gen-changelog.py', meson.project_name(), '1.20.0', meson.project_version())
