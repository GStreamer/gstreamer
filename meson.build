project('gstreamer-full', 'c',
  version : '1.23.0.1',
  meson_version : '>= 1.1',
  default_options : ['buildtype=debugoptimized',
                     # Needed due to https://github.com/mesonbuild/meson/issues/1889,
                     # but this can cause problems in the future. Remove it
                     # when it's no longer necessary.
                     'cpp_std=c++14'])

apiversion = '1.0'
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

gitlint_req = '>= 0.18'
gitlint = find_program('gitlint', version: gitlint_req, required: false)
if gitlint.found()
  run_command(gitlint, 'install-hook', check: false)
else
  message('gitlint not found or too old, please install it with your package manager or `python3 -m pip install gitlint` to enable the commit message hook')
endif

# On macOS, you have to run "Install Certificates.command" otherwise Python
# doesn't have access to the latest SSL CA Certificates, and Meson will fail to
# download wrap files from websites that use, for example, Let's Encrypt.
# We already recommend this in the README, but add a warning here as well.
# Can't make this an error because the user might be using XCode's Python
# 3 which doesn't have this script.
if build_system == 'darwin'
  python3_cacert_file = python3.get_path('data') / 'etc/openssl/cert.pem'
  install_cert_cmd = '/Applications/Python @0@/Install Certificates.command'.format(python3.language_version())
  if not fs.is_symlink(python3_cacert_file) and fs.is_file(install_cert_cmd)
    warning('Please run "@0@" so that Python has access to the latest SSL certificates. Meson might fail to download some wraps without it.'.format(install_cert_cmd))
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

building_full = get_option('default_library') == 'static'
building_full_static = get_option('gst-full-target-type') == 'static_library'
building_full_options = []
if building_full
building_full_options = ['gstreamer-full=enabled']
endif

libnice_options = []
if get_option('webrtc').enabled()
  libnice_options += ['gstreamer=enabled']
endif

# Disable gst-python if we've disabled introspection
gst_python_option = get_option('python').disable_if(get_option('introspection').disabled())

# Ordered list of subprojects (dict has no ordering guarantees)
subprojects = [
  ['gstreamer', {'build-hotdoc': true, 'subproject_options': building_full_options}],
  ['gst-plugins-base', {'option': get_option('base'), 'build-hotdoc': true}],
  ['gst-plugins-good', {'option': get_option('good'), 'build-hotdoc': true}],
  ['libnice', { 'option': get_option('libnice'), 'match_gst_version': false, 'subproject_options': libnice_options}],
  ['gst-plugins-bad', { 'option': get_option('bad'), 'build-hotdoc': true}],
  ['gst-plugins-ugly', { 'option': get_option('ugly'), 'build-hotdoc': true}],
  ['gst-libav', { 'option': get_option('libav'), 'build-hotdoc': true}],
  ['gst-rtsp-server', { 'option': get_option('rtsp_server'), 'build-hotdoc': true}],
  ['gst-devtools', { 'option': get_option('devtools'), 'build-hotdoc': true}],
  ['gst-integration-testsuites', { 'option': get_option('devtools') }],
  ['gst-editing-services', { 'option': get_option('ges'), 'build-hotdoc': true}],
  ['gstreamer-vaapi', { 'option': get_option('vaapi'), 'build-hotdoc': true}],
  ['gstreamer-sharp', { 'option': get_option('sharp') }],
  ['pygobject', { 'option': get_option('python'), 'match_gst_version': false, 'sysdep': 'pygobject-3.0', 'sysdep_version': '>= 3.8' }],
  ['gst-python', { 'option': gst_python_option}],
  ['gst-examples', { 'option': get_option('gst-examples'), 'match_gst_versions': false}],
  ['gst-plugins-rs', { 'option': get_option('rs'), 'build-hotdoc': true, 'match_gst_version': false}],
]

if get_option('build-tools-source') == 'subproject'
  if build_system == 'windows'
    subproject('win-flex-bison-binaries')
    subproject('win-nasm')
    subproject('win-pkgconfig')
  elif build_system == 'darwin'
    subproject('macos-bison-binary')
    # Newer macOS provides /usr/lib/pkgconfig/libpcre2-8.pc which is broken
    # because it says headers are in /usr/include but that directory doesn't
    # exist. It can only be used to find the library, which only exists on
    # newer macOS at /usr/lib/libpcre2-8.dylib, so it's also unusable.
    #
    # jit support requires macOS 11.0 or newer, so disable it by default
    subproject('pcre2', default_options: ['default_library=static', 'jit=disabled'])
  endif
endif

orc_option = get_option('orc')
# There is a check below to keep this in sync with subprojects/gst-plugins-base/meson.build
orc_req = '>= 0.4.24'
orc_source_option = get_option('orc-source')
orc_subproject = disabler()
if orc_option.allowed()
  if orc_source_option == 'subproject'
    orc_subproject = subproject('orc', required: orc_option)
  else
    dependency('orc-0.4', version: orc_req, required: orc_option,
               allow_fallback: orc_source_option == 'auto')
  endif
endif

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
all_tools = {}
all_helpers = {}
gst_tools = []
gst_helpers = []
gst_libraries =[]
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
  default_options =  build_infos.get('subproject_options', [])

  if match_gst_version
    subproj = subproject(project_name, version: gst_version, required: is_required, default_options: default_options)
  elif sysdep != ''
    sysdep_dep = dependency(sysdep, version: sysdep_version, required: is_required, default_options: default_options)
  else
    subproj = subproject(project_name, required: is_required, default_options: default_options)
  endif

  if project_name == 'gst-plugins-base' and subproj.found()
    gst_base_orc_req = subproj.get_variable('orc_req', '')
    if gst_base_orc_req != orc_req
      error('orc_req is "@0@" but it should be "@1@" from subprojects/gst-plugins-base/meson.build'
            .format(orc_req, gst_base_orc_req))
    endif
  endif

  if subproj.found()
    plugins = subproj.get_variable('gst_plugins', [])
    legacy_plugins = subproj.get_variable('plugins', [])
    all_plugins += plugins
    if plugins.length() == 0 and legacy_plugins.length() > 0
      warning(f'DEPRECATED use of the `plugins` variable in @project_name@.')
      warning('The variable should now be called `gst_plugins` and use:')
      warning('`declare_dependency( link_with: <plugin_target>, variable: {\'full_path\': <plugin_target>.full_path()})` instead')
      foreach plugin: legacy_plugins
        all_plugins += [declare_dependency(link_with: plugin, variables: {'full_path': plugin.full_path()})]
      endforeach
    endif

    all_libraries += subproj.get_variable('gst_libraries', [])
    if not get_option('tools').disabled()
      all_tools += subproj.get_variable('gst_tools', {})
      all_helpers += subproj.get_variable('gst_helpers', {})
    endif

    orc_update_targets += subproj.get_variable('orc_update_targets', [])

    subprojects_names += [project_name]

    if not meson.is_cross_build() and build_infos.get('build-hotdoc', false)
      plugins_doc_caches += [subproj.get_variable('gst_plugins_doc_dep', [])]
      if documented_projects != ''
        documented_projects += ','
      endif
      documented_projects  += project_name
    endif
  endif
endforeach

# Check if we need to also build glib-networking for TLS modules
giomodules = []
glib_dep = dependency('glib-2.0')
if glib_dep.type_name() == 'internal'
  subp = subproject('glib-networking', required : get_option('tls'),
             default_options: ['gnutls=auto', 'openssl=auto'])
  if subp.found()
    giomodules += subp.get_variable('giomodules', [])
  endif
endif

gst_plugins_doc_dep = custom_target('plugins-doc-cache',
  command: [python3, '-c', 'print("Built all doc caches")'],
  input: plugins_doc_caches,
  output: 'plugins_doc_caches',
  capture: true,
)

gir_files = []
gir_targets = []
foreach pkgname_library : all_libraries
  lib_def = pkgname_library[1]
  foreach gir_target: lib_def.get('gir_targets', [])
    gir_files += [gir_target[0].full_path()]
    gir_targets += gir_target
  endforeach
endforeach

custom_target('update_girs',
  command: [find_program('scripts/update-girs.py'), meson.global_build_root()] + gir_files,
  output: 'update_girs',
  capture: true,
  depends: gir_targets
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
  gst_doc = subproject('gst-docs', required: get_option('doc').enabled())
  if gst_doc.found()
    gst_doc_target = gst_doc.get_variable('gstreamer_doc')
    alias_target('gst-doc', gst_doc_target)
  endif
  message('Gst docs subprojects: ' + documented_projects)
endif

all_plugins_paths = []
all_plugins_dirs = []
plugins_names = []
foreach plugin: all_plugins
  plugin_path = plugin.get_variable('full_path')
  all_plugins_paths += plugin_path
  all_plugins_dirs += fs.parent(plugin_path)
  plugins_names += fs.name(plugin_path)
endforeach

# Work around meson bug: https://github.com/mesonbuild/meson/pull/6770
pathsep = host_machine.system() == 'windows' ? ';' : ':'
all_plugins_paths = pathsep.join(all_plugins_paths)

devenv = environment()
if not building_full
  devenv.prepend('GST_PLUGIN_PATH', all_plugins_dirs)
else
  # Make sure the current build directory is first in PATH so we prefer tools
  # built here that links on gst-full instead instead of those built in
  # subprojects.
  devenv.prepend('PATH', meson.current_build_dir())
endif
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

if building_full
  cdata = configuration_data()
  cdata.set_quoted('GST_API_VERSION', apiversion)
  cdata.set_quoted('GETTEXT_PACKAGE', 'gstreamer-full-1.0')
  cdata.set_quoted('PACKAGE_VERSION', meson.project_version())
  cdata.set_quoted('GST_PACKAGE_ORIGIN', get_option('package-origin'))
  cdata.set_quoted('GST_FULL_LICENSE', get_option('gstreamer-full-license'))
  cdata.set_quoted('GST_PLUGIN_FULL_FEATURES_NAME', 'fullstaticfeatures')
  configure_file(output : 'config.h', configuration : cdata)
  configinc = include_directories('.')
  gst_c_args = ['-DHAVE_CONFIG_H']

  # Generate a .c file which declare and register all built plugins
  static_plugins = get_option('gst-full-plugins')
  plugins_to_be_initialized = []
  if static_plugins == '*'
    all_plugin_names = ';'.join(plugins_names)
  else
    missing_plugins = []
    foreach plugin : static_plugins.split(';')
      if plugins_names.contains(plugin)
        plugins_to_be_initialized += [plugin]
      else
        missing_plugins += [plugin]
      endif
    endforeach
    if missing_plugins.length() > 0
      error('Some gstreamer-full plugins enabled via the \'gst-full-plugins\' option are not available, please enable the corresponding meson options: @0@'.format(', '.join(missing_plugins)))
    endif
    all_plugin_names = ';'.join(plugins_to_be_initialized)
  endif
  generate_init_static_plugins = find_program('scripts/generate_init_static_plugins.py')
  init_static_plugins_c = configure_file(
    output: 'gstinitstaticplugins.c',
    command : [generate_init_static_plugins,
               '-o ' + '@OUTPUT@',
               '-p ' + all_plugin_names,
               '-e ' + get_option('gst-full-elements'),
               '-t ' + get_option('gst-full-typefind-functions'),
               '-d ' + get_option('gst-full-device-providers'),
               '-T ' + get_option('gst-full-dynamic-types'),
               '--giomodules', ';'.join(giomodules),
               ]
  )

  gstfull_link_args = cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions'])

  # Get a list of libraries that needs to be exposed in the ABI.
  exposed_libs = []
  exposed_deps = []
  exposed_girs = []
  incdir_deps = []
  wanted_libs = ['gstreamer-1.0'] + get_option('gst-full-libraries')
  all_libs = '*' in wanted_libs

  foreach pkgname_library : all_libraries
    pkg_name = pkgname_library[0]
    gst_libraries += pkg_name
    lib_def = pkgname_library[1]

    if pkg_name in wanted_libs or all_libs
      if lib_def.has_key('lib')
        exposed_deps += dependency(pkg_name)
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
  # Note that link_whole dependencies aren't exposed transitively in
  # Windows/Cygwin, because symbols from static libraries must
  # be manually marked as exported through a module definition file.
  # See e.g. https://github.com/FFmpeg/FFmpeg/blob/3057ce797f6e1348b978f5ffe9e2afd2224544f0/configure#L5726
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

  giomodules_deps = []
  foreach module : giomodules
    giomodules_deps += dependency(module)
  endforeach

  # Build shared library
  gstfull = build_target('gstreamer-full-1.0',
    init_static_plugins_c,
    c_args: ['-DBUILDING_GST'] + gst_c_args,
    target_type: get_option('gst-full-target-type'),
    link_args: gstfull_link_args,
    link_whole : exposed_libs,
    dependencies : [incdir_deps, glib_deps, all_plugins, giomodules_deps],
    link_depends : link_deps,
    install : true,
  )
  gst_full_c_flags =  []
  if building_full_static or get_option('default_library') == 'static'
    gst_full_c_flags += ['-DGST_STATIC_COMPILATION']
  endif

  gst_full_libs = [gstfull]

  # See above re: symbol exports in Win32
  if ['windows', 'cygwin'].contains(host_machine.system())
    gst_full_libs += exposed_libs
  endif

  gst_full_dep = declare_dependency(link_with: gst_full_libs,
    compile_args: gst_full_c_flags,
    dependencies : incdir_deps + glib_deps,
    include_directories: include_directories('.')
  )

  gst_full_libs_private = cc.get_supported_link_arguments(['-Wl,--undefined=gst_init_static_plugins'])
  if gst_full_libs_private == []
    warning('The compiler does not support `-Wl,--undefined` linker flag. The method `gst_init_static_plugins` might be dropped during the link stage of an application using libgstreamer-full-1.0.a, preventing plugins registration.')
  endif

  if building_full_static
    warning('Introspection is enabled in gst-full static build mode but this is not supported by now. gir generation is now disabled.')
  endif
  # FIXME: gnome.generate_gir should link with not only gstfull in static gst-full but the whole gstreamer suite.
  # tried dependencies without success.
  if not get_option('introspection').disabled() and not building_full_static
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
    extra_cflags: gst_full_c_flags,
    subdirs : 'gstreamer-1.0')
  meson.override_dependency('gstreamer-full-1.0', gst_full_dep)

  if not get_option('tools').disabled()
    # Loop for tools
    foreach tool, data: all_tools
      gst_tools += tool
      exe_name = '@0@-@1@'.format(tool, apiversion)

      deps = []
      foreach d : data.get('deps', [])
        if d not in exposed_deps
          deps += d
        endif
      endforeach

      exe = executable(exe_name,
        data.get('files'),
        install: data.get('install', true),
        install_tag: data.get('install_tag', 'bin'),
        install_dir: data.get('install_dir', get_option('bindir')),
        include_directories : data.get('include_directories', [configinc]),
        dependencies : [gst_full_dep] + deps,
        c_args: data.get('extra_c_args', []) + gst_c_args + ['-DG_LOG_DOMAIN="@0@"'.format(exe_name)],
        cpp_args: data.get('extra_cpp_args', []),
        objc_args: data.get('extra_objc_args', []),
        override_options: data.get('override_options', []),
      )

      if(data.has_key('env'))
        env = data.get('env')
        value = env[1]
        if env[1] == 'exe-full-path'
          value = exe.full_path()
        endif
        meson.add_devenv({env[0]: value})
      endif

      if data.has_key('man_page')
        install_man(data.get('man_page'))
      endif

    endforeach

    # Loop for helpers tools
    foreach helper, data: all_helpers
      gst_helpers += helper
      exe_name = '@0@'.format(helper)

      deps = []
      foreach d : data.get('deps', [])
        if d not in exposed_deps
          deps += d
        endif
      endforeach

      exe = executable(exe_name,
        data.get('files'),
        install: data.get('install', true),
        install_tag: data.get('install_tag', 'bin'),
        install_dir: data.get('install_dir', get_option('bindir')),
        include_directories : data.get('include_directories', [configinc]),
        dependencies : [gst_full_dep] + deps,
        c_args: data.get('extra_c_args', []) + gst_c_args + ['-DG_LOG_DOMAIN="@0@"'.format(exe_name)],
        cpp_args: data.get('extra_cpp_args', []),
        objc_args: data.get('extra_objc_args', []),
        override_options: data.get('override_options', []),
      )

      if(data.has_key('env'))
        env = data.get('env')
        value = env[1]
        if env[1] == 'exe-full-path'
          value = exe.full_path()
        endif
        meson.add_devenv({env[0]: value})
      endif

      if data.has_key('man_page')
        install_man(data.get('man_page'))
      endif

    endforeach
  endif
else # not gst_full
  foreach tool, data: all_tools
    gst_tools += tool
  endforeach
  foreach helper, data: all_helpers
    gst_helpers += helper
  endforeach
  foreach library : all_libraries
    gst_libraries = library[0]
  endforeach
endif

message('Building subprojects: ' + ', '.join(subprojects_names))

setenv = find_program('gst-env.py')

devenv_cmd = [setenv, '--builddir=@0@'.format(meson.global_build_root()),
              '--srcdir=@0@'.format(meson.global_source_root())]

subdir('tests')
subdir('ci/fuzzing')

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

subdir('scripts')

dotnet_format = find_program('dotnet-format', required: false)
if dotnet_format.found()
    run_target('csharp_format_check',
        command: [join_paths(meson.current_source_dir(), 'scripts', 'format-csharp'),
            '--check'
        ],
    )
    run_target('csharp_format_apply',
        command: [join_paths(meson.current_source_dir(), 'scripts', 'format-csharp'),
        ],
    )
endif

summary({
  'gstreamer-full library': building_full,
  'gstreamer-full target type': get_option('gst-full-target-type'),
  'Tools': gst_tools,
  'Helpers': gst_helpers,
  'Libraries': gst_libraries,
  'Tests and examples disabled': building_full,
}, section: 'Build options', bool_yn: true, list_sep: '  ')
