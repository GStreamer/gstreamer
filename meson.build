project('All GStreamer modules', 'c',
  version : '1.18.5',
  meson_version : '>= 0.52.0',
  default_options : ['buildtype=debugoptimized'])

gst_version = '>= @0@'.format(meson.project_version())
gst_branch = 'master'

build_system = build_machine.system()
cc = meson.get_compiler('c')

pkgconfig = import('pkgconfig')
python3 = import('python').find_installation()
# Ensure that we're not being run from inside the gst-uninstalled env
# because that will confuse meson, and it might find the already-built
# gstreamer. It's fine if people run `ninja` as long as it doesn't run
# reconfigure because ninja doesn't care about the env.
ensure_not_uninstalled = '''
import os
assert('GST_ENV' not in os.environ)
'''
cmdres = run_command(python3, '-c', ensure_not_uninstalled)
if cmdres.returncode() != 0
  error('Do not run `ninja` or `meson` for gst-build inside the uninstalled environment, you will run into problems')
endif

# Ensure that the user does not have Strawberry Perl in PATH, since it ships
# with a pkg-config.bat and broken pkgconfig files for libffi and zlib. Will
# cause a build error, such as in
# https://gitlab.freedesktop.org/gstreamer/gst-build/-/issues/41
ensure_no_strawberry_perl = '''
import os
assert(r'Strawberry\perl\bin' not in os.environ['PATH'])
'''
if build_system == 'windows'
  cmdres = run_command(python3, '-c', ensure_no_strawberry_perl)
  if cmdres.returncode() != 0
    error('You have Strawberry Perl in PATH which is known to cause build issues with gst-build. Please remove it from PATH or uninstall it.')
  endif
endif

documented_projects = ''
# Make it possible to use msys2 built zlib which fails
# when not using the mingw toolchain as it uses unistd.h
if not meson.is_subproject() and cc.get_id() == 'msvc'
  uname = find_program('uname', required: false)
  if uname.found()
    ret = run_command(uname, '-o')
    if ret.returncode() == 0 and ret.stdout().to_lower() == 'msys'
      ret = run_command(uname, '-r')
      # The kernel version returned by uname is actually the msys version
      if ret.returncode() == 0 and ret.stdout().startswith('2')
        # If a system zlib is found, disable UNIX features in zlib.h and zconf.h
        if cc.find_library('z').found()
          add_global_arguments('-DZ_SOLO', language: 'c')
        endif
      endif
    endif
  endif

  # Change some warning which belong to level 3 (production quality) or
  # 4 (informational) to level 1 (severe)
  add_global_arguments (
      '/w14062', # enumerator 'identifier' in switch of enum 'enumeration' is not handled
      '/w14101', # 'identifier' : unreferenced local variable
      '/w14189', # 'identifier' : local variable is initialized but not referenced
      cc.get_supported_arguments(['/utf-8']), # set the input encoding to utf-8
      language: ['c', 'cpp'])
endif

# Ordered list of subprojects (dict has no ordering guarantees)
subprojects = [
  ['gstreamer', {'build-hotdoc': true}],
  ['gst-plugins-base', {'build-hotdoc': true}],
  ['gst-plugins-good', {'build-hotdoc': true}],
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
  ['pygobject', { 'option': get_option('python'), 'match_gst_version': false }],
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
endif

orc_subproject = subproject('orc', required: get_option('orc'))

subprojects_names = []
plugins_doc_caches = []
orc_update_targets = []
all_plugins = []
foreach sp : subprojects
  project_name = sp[0]
  build_infos = sp[1]
  is_required = build_infos.get('option', true)
  match_gst_version = build_infos.get('match_gst_version', true)

  if match_gst_version
    subproj = subproject(project_name, version: gst_version, required: is_required)
  else
    subproj = subproject(project_name, required: is_required)
  endif

  if subproj.found()
    plugins = subproj.get_variable('plugins', [])
    all_plugins += plugins

    orc_update_targets += subproj.get_variable('orc_update_targets', [])

    subprojects_names += [project_name]
    cmdres = run_command(python3, '-c', symlink.format(project_name, meson.current_source_dir()))
    if cmdres.returncode() == 0
      message('Created symlink to ' + project_name)
    endif

    if not meson.is_cross_build() and build_infos.get('build-hotdoc', false)
      if plugins.length() > 0
        plugins_doc_caches += [subproj.get_variable('plugins_doc_dep')]
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

foreach custom_subproj: get_option('custom_subprojects').split(',')
    if custom_subproj != ''
        message ('Adding custom subproject ' + custom_subproj)
        subproject(custom_subproj)
        subprojects_names += [custom_subproj]
    endif
endforeach

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
foreach plugin: all_plugins
  all_plugins_paths += plugin.full_path()
endforeach
# Work around meson bug: https://github.com/mesonbuild/meson/pull/6770
pathsep = host_machine.system() == 'windows' ? ';' : ':'
all_plugins_paths = pathsep.join(all_plugins_paths)

generate_plugins_paths = find_program('scripts/generate_plugins_path.py')
configure_file(
  output : 'GstPluginsPath.json',
  command : [generate_plugins_paths,
             '@OUTPUT@',
             all_plugins_paths]
)

# FIXME: Create a 'libraries' list in each subproject like we do for 'plugins'
libraries_map = {
  # name: [subproject_name, variable_name]
  'gstreamer': ['gstreamer', 'libgst'],
  'base': ['gstreamer', 'gst_base'],
  'check': ['gstreamer', 'gst_check'],
  'controller': ['gstreamer', 'gst_controller'],
  'net': ['gstreamer', 'gst_net'],

  'allocators': ['gst-plugins-base', 'gstallocators'],
  'app': ['gst-plugins-base', 'gstapp'],
  'audio': ['gst-plugins-base', 'gstaudio'],
  'fft': ['gst-plugins-base', 'gstfft'],
  'pbutils': ['gst-plugins-base', 'pbutils'],
  'riff': ['gst-plugins-base', 'gstriff'],
  'rtp': ['gst-plugins-base', 'gst_rtp'],
  'rtsp': ['gst-plugins-base', 'gst_rtsp'],
  'sdp': ['gst-plugins-base', 'gstsdp'],
  'tag': ['gst-plugins-base', 'gsttag'],
  'video': ['gst-plugins-base', 'gstvideo'],
  'gl': ['gst-plugins-base', 'gstgl'],

  'bad-audio': ['gst-plugins-bad', 'gstbadaudio'],
  'bad-transcoder': ['gst-plugins-bad', 'gst_transcoder'],
  'codecparsers': ['gst-plugins-bad', 'gstcodecparsers'],
  'insertbin': ['gst-plugins-bad', 'gstinsertbin'],
  'mpegts': ['gst-plugins-bad', 'gstmpegts'],
  'player': ['gst-plugins-bad', 'gstplayer'],
  'sctp': ['gst-plugins-bad', 'libgstsctp'],
  'webrtc': ['gst-plugins-bad', 'gstwebrtc'],
  'vulkan': ['gst-plugins-bad', 'gstvulkan'],

  'rtsp-server': ['gst-rtsp-server', 'gst_rtsp_server'],
}

if get_option('default_library') == 'static'
  # Generate a .c file which declare and register all built plugins
  generate_init_static_plugins = find_program('scripts/generate_init_static_plugins.py')
  init_static_plugins_c = configure_file(
    output: 'gstinitstaticplugins.c',
    command : [generate_init_static_plugins,
               '@OUTPUT@',
               all_plugins_paths]
  )
  install_headers('gstinitstaticplugins.h', subdir : 'gstreamer-1.0/gst')

  # Avoid a x264 link issue described here: https://gitlab.freedesktop.org/gstreamer/gst-build/-/issues/108
  # Similar issue has been found with VLC: https://mailman.videolan.org/pipermail/vlc-devel/2009-March/057640.html
  gstfull_link_args = []
  if cc.has_link_argument('-Wl,-Bsymbolic')
    gstfull_link_args += ['-Wl,-Bsymbolic']
  endif


  gst_dep = subproject('gstreamer').get_variable('gst_dep')

  # Get a list of libraries that needs to be exposed in the ABI.
  exposed_libs = []
  foreach name : get_option('gst-full-libraries') + ['gstreamer']
    info = libraries_map[name]
    exposed_libs += subproject(info[0]).get_variable(info[1])
  endforeach

  # glib and gobject are part of our public API. If we are using glib from the
  # system then our pkg-config file must require it. If we built it as
  # subproject then we need to link_whole it.
  requires = []
  gobject_dep = dependency('gobject-2.0', fallback: ['glib', 'libgobject_dep'])
  if gobject_dep.type_name() == 'internal'
      glib_subproject = subproject('glib')
      exposed_libs += glib_subproject.get_variable('libglib')
      exposed_libs += glib_subproject.get_variable('libgobject')
  else
      requires = ['glib-2.0', 'gobject-2.0']
  endif

  # Build both shared and static library
  gstfull = both_libraries('gstreamer-full-1.0',
    init_static_plugins_c,
    link_with : all_plugins,
    link_args: gstfull_link_args,
    link_whole : exposed_libs,
    dependencies : gst_dep,
    install : true,
  )
  pkgconfig.generate(gstfull,
    requires: requires,
    subdirs : 'gstreamer-1.0')
endif

message('Building subprojects: ' + ', '.join(subprojects_names))
setenv = find_program('gst-env.py')

devenv_cmd = [setenv, '--builddir=@0@'.format(meson.build_root()),
              '--srcdir=@0@'.format(meson.source_root())]

if meson.has_exe_wrapper() and build_machine.system() == 'linux' and host_machine.system() == 'windows'
  # FIXME: Ideally we could get the wrapper directly from meson
  devenv_cmd += ['--wine', host_machine.cpu_family() == 'x86_64' ? 'wine64' : 'wine32']
  sysroot = meson.get_cross_property('sys_root')
  if sysroot != ''
    # Logic from meson
    devenv_cmd += ['--winepath', 'Z:' + join_paths(sysroot, 'bin')]
  endif
endif

run_target('uninstalled', command : devenv_cmd)
run_target('devenv', command : devenv_cmd)

update = find_program('git-update')
run_target('git-update', command : [update])
run_target('update', command : [update,
    '--builddir=@0@'.format(meson.current_build_dir())])

if orc_subproject.found() and orc_update_targets.length() > 0
  alias_target('update-orc-dist', orc_update_targets)
endif
