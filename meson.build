project('gstreamer-full', 'c',
  version : '1.20.2.1',
  meson_version : '>= 0.59',
  default_options : ['buildtype=debugoptimized',
                     # Needed due to https://github.com/mesonbuild/meson/issues/1889,
                     # but this can cause problems in the future. Remove it
                     # when it's no longer necessary.
                     'cpp_std=c++14'])

gst_version = '>= @0@'.format(meson.project_version())

build_system = build_machine.system()
cc = meson.get_compiler('c')

fs = import('fs')
gnome = import('gnome')
pkgconfig = import('pkgconfig')
python3 = import('python').find_installation()
# Ensure that we're not being run from inside the development environment
# because that will confuse meson, and it might find the already-built
# gstreamer. It's fine if people run `ninja` as long as it doesn't run
# reconfigure because ninja doesn't care about the env.
ensure_not_devenv = '''
import os
assert('GST_ENV' not in os.environ)
'''
cmdres = run_command(python3, '-c', ensure_not_devenv, check: false)
if cmdres.returncode() != 0
  error('Do not run `ninja reconfigure` or `meson` for gst-build inside the development environment, you will run into problems')
endif

# Install gst-indent pre-commit hook
run_command(python3, '-c', 'import shutil; shutil.copy("scripts/git-hooks/multi-pre-commit.hook", ".git/hooks/pre-commit")', check: false)

# Ensure that the user does not have Strawberry Perl in PATH, since it ships
# with a pkg-config.bat and broken pkgconfig files for libffi and zlib. Will
# cause a build error, such as in
# https://gitlab.freedesktop.org/gstreamer/gst-build/-/issues/41
ensure_no_strawberry_perl = '''
import os
assert(r'Strawberry\perl\bin' not in os.environ['PATH'])
'''
if build_system == 'windows' and meson.version().version_compare('<0.60.0')
  cmdres = run_command(python3, '-c', ensure_no_strawberry_perl, check: false)
  if cmdres.returncode() != 0
    error('You have Strawberry Perl in PATH which is known to cause build issues with Meson < 0.60.0. Please remove it from PATH, uninstall it, or upgrade Meson.')
  endif
endif

documented_projects = ''
# Make it possible to use msys2 built zlib which fails
# when not using the mingw toolchain as it uses unistd.h
if not meson.is_subproject() and cc.get_id() == 'msvc'
  uname = find_program('uname', required: false)
  if uname.found()
    ret = run_command(uname, '-o', check: false)
    if ret.returncode() == 0 and ret.stdout().to_lower() == 'msys'
      ret = run_command(uname, '-r', check: false)
      # The kernel version returned by uname is actually the msys version
      if ret.returncode() == 0 and ret.stdout().startswith('2')
        # If a system zlib is found, disable UNIX features in zlib.h and zconf.h
        if cc.find_library('z').found()
          add_global_arguments('-DZ_SOLO', language: 'c')
        endif
      endif
    endif
  endif
endif

# Ensure that MSVC interprets all source code as UTF-8. Only do this when we're
# not a subproject, because subprojects are not allowed to call
# add_global_arguments().
if not meson.is_subproject() and cc.get_id() == 'msvc'
  add_global_arguments(
      cc.get_supported_arguments(['/utf-8']), # set the input encoding to utf-8
      language: ['c', 'cpp'])
endif

# Ordered list of subprojects (dict has no ordering guarantees)
subprojects = [
  ['gstreamer', {'build-hotdoc': true}],
  ['gst-plugins-base', {'option': get_option('base'), 'build-hotdoc': true}],
  ['gst-plugins-good', {'option': get_option('good'), 'build-hotdoc': true}],
  ['libnice', { 'option': get_option('libnice'), 'match_gst_version': false}],
  ['gst-plugins-bad', { 'option': get_option('bad'), 'build-hotdoc': true}],
  ['gst-plugins-ugly', { 'option': get_option('ugly'), 'build-hotdoc': true}],
  ['gst-libav', { 'option': get_option('libav'), 'build-hotdoc': true}],
  ['gst-rtsp-server', { 'option': get_option('rtsp_server'), 'build-hotdoc': true}],
  ['gst-devtools', { 'option': get_option('devtools'), 'build-hotdoc': true }],
  ['gst-integration-testsuites', { 'option': get_option('devtools') }],
  ['gst-editing-services', { 'option': get_option('ges'), 'build-hotdoc': true}],
  ['gstreamer-vaapi', { 'option': get_option('vaapi'), 'build-hotdoc': true}],
  ['gst-omx', { 'option': get_option('omx'), 'build-hotdoc': true}],
  ['gstreamer-sharp', { 'option': get_option('sharp') }],
  ['pygobject', { 'option': get_option('python'), 'match_gst_version': false, 'sysdep': 'pygobject-3.0', 'sysdep_version': '>= 3.8' }],
  ['gst-python', { 'option': get_option('python')}],
  ['gst-examples', { 'option': get_option('gst-examples'), 'match_gst_versions': false}],
  ['gst-plugins-rs', { 'option': get_option('rs'), 'match_gst_version': false}],
]

symlink = '''
import os

os.symlink(os.path.join('@1@', 'subprojects', '@0@'),
  os.path.join('@1@', '@0@'))
'''

if build_system == 'windows'
  subproject('win-flex-bison-binaries')
  subproject('win-nasm')
elif build_system == 'darwin'
  subproject('macos-bison-binary')
endif

orc_subproject = subproject('orc', required: get_option('orc'))

foreach custom_subproj: get_option('custom_subprojects').split(',')
    if custom_subproj != ''
        message ('Adding custom subproject ' + custom_subproj)
        subprojects += [[custom_subproj, {'match_gst_version': false}]]
    endif
endforeach


subprojects_names = []
plugins_doc_caches = []
orc_update_targets = []
all_plugins = []
# Using a list and not a dict to keep the ordering to build the chain of `gir`
# dependencies
all_libraries = []
foreach sp : subprojects
  project_name = sp[0]
  build_infos = sp[1]
  is_required = build_infos.get('option', true)
  sysdep = build_infos.get('sysdep', '')
  sysdep_version = build_infos.get('sysdep_version', '')
  match_gst_version = build_infos.get('match_gst_version', true)

  if match_gst_version
    subproj = subproject(project_name, version: gst_version, required: is_required)
  elif sysdep != ''
      sysdep_dep = dependency(sysdep, version: sysdep_version, required: false)
      if not sysdep_dep.found()
        subproj = subproject(project_name, required: is_required)
      endif
  else
    subproj = subproject(project_name, required: is_required)
  endif

  if subproj.found()
    plugins = subproj.get_variable('plugins', [])
    all_plugins += plugins
    all_libraries += subproj.get_variable('libraries', [])

    orc_update_targets += subproj.get_variable('orc_update_targets', [])

    subprojects_names += [project_name]

    if not meson.is_cross_build() and build_infos.get('build-hotdoc', false)
      if plugins.length() > 0
        plugins_doc_caches += [subproj.get_variable('plugins_doc_dep', [])]
      endif
      if documented_projects != ''
        documented_projects += ','
      endif
      documented_projects  += project_name
    endif
  endif
endforeach

# Check if we need to also build glib-networking for TLS modules
glib_dep = dependency('glib-2.0')
if glib_dep.type_name() == 'internal'
  subproject('glib-networking', required : get_option('tls'),
             default_options: ['gnutls=auto', 'openssl=auto'])
endif

plugins_doc_dep = custom_target('plugins-doc-cache',
  command: [python3, '-c', 'print("Built all doc caches")'],
  input: plugins_doc_caches,
  output: 'plugins_doc_caches',
  capture: true,
)

if meson.is_cross_build() or build_machine.system() == 'windows'
    if get_option('doc').enabled()
        error('Documentation enabled but building the doc while cross building or building on windows is not supported yet.')
    endif

    documented_projects = ''
    message('Documentation not built as building the documentation while cross building or building on windows is not supported yet.')
else
  hotdoc_p = find_program('hotdoc', required : get_option('doc'))
  if not hotdoc_p.found()
    documented_projects = ''
    message('Not building documentation as hotdoc was not found')
  endif
endif

write_file_contents = '''
import os
import sys

assert len(sys.argv) >= 3
fname = sys.argv[1]
contents = sys.argv[2]

with open(fname, 'w') as f:
    f.write(contents)
'''

configure_file(
  output : 'GstDocumentedSubprojects',
  command : [python3,
             '-c', write_file_contents,
             '@OUTPUT@',
             documented_projects]
)

if documented_projects != ''
  subproject('gst-docs', required: get_option('doc').enabled())
  message('Gst docs subprojects: ' + documented_projects)
endif

all_plugins_paths = []
all_plugins_dirs = []
foreach plugin: all_plugins
  all_plugins_paths += plugin.full_path()
  all_plugins_dirs += fs.parent(plugin.full_path())
endforeach
# Work around meson bug: https://github.com/mesonbuild/meson/pull/6770
pathsep = host_machine.system() == 'windows' ? ';' : ':'
all_plugins_paths = pathsep.join(all_plugins_paths)

devenv = environment()
devenv.set('GST_PLUGIN_PATH', all_plugins_dirs)
devenv.set('CURRENT_GST', meson.current_source_dir())
devenv.set('GST_VERSION', meson.project_version())
devenv.set('GST_ENV', 'gst-' + meson.project_version())
devenv.set('GST_REGISTRY', meson.current_build_dir() / 'registry.dat')
devenv.set('GST_PLUGIN_SYSTEM_PATH', '')
meson.add_devenv(devenv)

generate_plugins_paths = find_program('scripts/generate_plugins_path.py')
configure_file(
  output : 'GstPluginsPath.json',
  command : [generate_plugins_paths,
             '@OUTPUT@',
             all_plugins_paths]
)

if get_option('default_library') == 'static'
  # Generate a .c file which declare and register all built plugins
  plugins_names = []
  foreach plugin: all_plugins
    plugins_names += plugin.full_path()
  endforeach
  all_plugin_names = ';'.join(plugins_names)

  static_plugins = get_option('gst-full-plugins')
  if static_plugins == '*'
    static_plugins = all_plugin_names
  endif
  generate_init_static_plugins = find_program('scripts/generate_init_static_plugins.py')
  init_static_plugins_c = configure_file(
    output: 'gstinitstaticplugins.c',
    command : [generate_init_static_plugins,
               '-o ' + '@OUTPUT@',
               '-p ' + static_plugins,
               '-e ' + get_option('gst-full-elements'),
               '-t ' + get_option('gst-full-typefind-functions'),
               '-d ' + get_option('gst-full-device-providers'),
               '-T ' + get_option('gst-full-dynamic-types')
               ]
  )

  gstfull_link_args = cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions'])

  # Get a list of libraries that needs to be exposed in the ABI.
  exposed_libs = []
  exposed_girs = []
  incdir_deps = []
  wanted_libs = ['gstreamer-1.0'] + get_option('gst-full-libraries')
  all_libs = '*' in wanted_libs

  foreach pkgname_library : all_libraries
    pkg_name = pkgname_library[0]
    lib_def = pkgname_library[1]

    if pkg_name in wanted_libs or all_libs
      if lib_def.has_key('lib')
        incdir_deps += dependency(pkg_name).partial_dependency(includes: true, sources: true)
        exposed_libs += [lib_def['lib']]
      endif

      if lib_def.has_key('gir')
        exposed_girs += lib_def['gir']
      endif
    endif
  endforeach

  # glib and gobject are part of our public API. If we are using glib from the
  # system then our pkg-config file must require it. If we built it as
  # subproject then we need to link_whole it.
  glib_deps = []
  glib_dep = dependency('glib-2.0')
  gobject_dep = dependency('gobject-2.0')
  if gobject_dep.type_name() == 'internal'
    glib_subproject = subproject('glib')
    exposed_libs += glib_subproject.get_variable('libglib')
    exposed_libs += glib_subproject.get_variable('libgobject')
    incdir_deps += [
      glib_dep.partial_dependency(includes: true),
      gobject_dep.partial_dependency(includes: true),
    ]
  else
    glib_deps = [glib_dep, gobject_dep]
  endif

  link_deps = []
  if get_option('gst-full-version-script') != ''
    symbol_map = meson.current_source_dir() / get_option('gst-full-version-script')
    link_arg = '-Wl,--version-script=' + symbol_map
    if cc.has_link_argument(link_arg)
      gstfull_link_args += link_arg
      link_deps += symbol_map
    elif cc.get_id() == 'msvc'
      warning('FIXME: Provide a def file to publish the public symbols')
    else
      warning('FIXME: Linker does not support the supplied version script (' + symbol_map + '), please disable the "gst-full-version-script" option')
    endif
  endif

  # Build both shared and static library
  gstfull = both_libraries('gstreamer-full-1.0',
    init_static_plugins_c,
    link_with : all_plugins,
    link_args: gstfull_link_args,
    link_whole : exposed_libs,
    dependencies : incdir_deps + glib_deps,
    link_depends : link_deps,
    install : true,
  )

  gst_full_dep = declare_dependency(link_with: gstfull.get_shared_lib(),
    dependencies : incdir_deps + glib_deps,
    include_directories: include_directories('.')
  )

  gst_full_libs_private = cc.get_supported_link_arguments(['-Wl,--undefined=gst_init_static_plugins'])
  if gst_full_libs_private == []
    warning('The compiler does not support `-Wl,--undefined` linker flag. The method `gst_init_static_plugins` might be dropped during the link stage of an application using libgstreamer-full-1.0.a, preventing plugins registration.')
  endif

  if not get_option('introspection').disabled()
    built_girs = {}
    foreach gir: exposed_girs
      includes = []
      foreach include: gir.get('includes', [])
        includes += [built_girs.get(include, include)]
      endforeach

      gir += {
        'includes': includes,
        'extra_args': gir.get('extra_args', []) + ['--add-include-path=' + meson.current_build_dir()],
        'install': true,
      }
      built_girs += {gir.get('namespace') + '-' + gir.get('nsversion'): gnome.generate_gir(gstfull, kwargs: gir)[0]}
    endforeach
  endif

  pkgconfig.generate(gstfull,
    requires: glib_deps,
    libraries_private: gst_full_libs_private,
    subdirs : 'gstreamer-1.0')
  meson.override_dependency('gstreamer-full-1.0', gst_full_dep)
endif

message('Building subprojects: ' + ', '.join(subprojects_names))

setenv = find_program('gst-env.py')

devenv_cmd = [setenv, '--builddir=@0@'.format(meson.build_root()),
              '--srcdir=@0@'.format(meson.source_root())]

subdir('tests')
if meson.can_run_host_binaries() and build_machine.system() == 'linux' and host_machine.system() == 'windows'
  # FIXME: Ideally we could get the wrapper directly from meson
  devenv_cmd += ['--wine', host_machine.cpu_family() == 'x86_64' ? 'wine64' : 'wine32']
  sysroot = meson.get_cross_property('sys_root')
  if sysroot != ''
    # Logic from meson
    devenv_cmd += ['--winepath', 'Z:' + join_paths(sysroot, 'bin')]
  endif
endif

run_target('devenv', command : devenv_cmd)

if orc_subproject.found() and orc_update_targets.length() > 0
  alias_target('update-orc-dist', orc_update_targets)
endif

summary({
  'gstreamer-full': get_option('default_library') == 'static',
}, section: 'Build options', bool_yn: true, list_sep: '  ')
