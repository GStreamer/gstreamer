project('All GStreamer modules', 'c',
  version : '1.17.0.1',
  meson_version : '>= 0.48.0',
  default_options : ['buildtype=debugoptimized'])

gst_version = '>= @0@'.format(meson.project_version())
gst_branch = 'master'

glib_req = '>= 2.44.0'

build_system = build_machine.system()
cc = meson.get_compiler('c')

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
      language: 'c')
endif

# Ordered list of subprojects (dict has no ordering guarantees)
subprojects = [
  ['gstreamer', {'build-hotdoc': true, 'has-plugins': true}],
  ['gst-plugins-base', {'build-hotdoc': true, 'has-plugins': true}],
  ['gst-plugins-good', {'build-hotdoc': true, 'has-plugins': true}],
  ['libnice', { 'option': get_option('libnice'), 'match_gst_version': false}],
  ['gst-plugins-bad', { 'option': get_option('bad'), 'build-hotdoc': true, 'has-plugins': true}],
  ['gst-plugins-ugly', { 'option': get_option('ugly'), 'build-hotdoc': true, 'has-plugins': true}],
  ['gst-libav', { 'option': get_option('libav'), 'build-hotdoc': true, 'has-plugins': true}],
  ['gst-rtsp-server', { 'option': get_option('rtsp_server'), 'build-hotdoc': true }],
  ['gst-devtools', { 'option': get_option('devtools'), 'build-hotdoc': true }],
  ['gst-integration-testsuites', { 'option': get_option('devtools') }],
  ['gst-editing-services', { 'option': get_option('ges'), 'build-hotdoc': true, 'has-plugins': true}],
  ['gstreamer-vaapi', { 'option': get_option('vaapi'), 'build-hotdoc': true, 'has-plugins': true}],
  ['gst-omx', { 'option': get_option('omx'), 'build-hotdoc': true, 'has-plugins': true}],
  ['gstreamer-sharp', { 'option': get_option('sharp') }],
  ['pygobject', { 'option': get_option('python'), 'match_gst_version': false }],
  ['gst-python', { 'option': get_option('python'), 'has-plugins': true}],
  ['gst-examples', { 'option': get_option('gst-examples'), 'match_gst_versions': false}],
  ['gst-plugins-rs', { 'option': get_option('rs'), 'has-plugins': true, 'match_gst_version': false}],
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

pathsep = host_machine.system() == 'windows' ? ';' : ':'

subproject('orc', required: get_option('orc'))

subprojects_names = []
plugins_doc_caches = []
all_plugins = ''
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
    # Replace by using subproject.get_variable('plugins', [])
    # when https://github.com/mesonbuild/meson/pull/5426/files
    # is merged and released
    if build_infos.has_key('has-plugins', default: false)
      plugins = subproj.get_variable('plugins')
    else
      plugins = []
    endif

    foreach plugin: plugins
      all_plugins += pathsep + plugin.full_path()
    endforeach

    subprojects_names += [project_name]
    cmdres = run_command(python3, '-c', symlink.format(project_name, meson.current_source_dir()))
    if cmdres.returncode() == 0
      message('Created symlink to ' + project_name)
    endif

    if not meson.is_cross_build() and build_infos.has_key('build-hotdoc', default: false)
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

    message('Documentation not built as building the documentation while cross building or building on windows is not supported yet.')
else
  hotdoc_p = find_program('hotdoc', required : get_option('doc'))
  if hotdoc_p.found()
    if documented_projects != ''
        subproject('gst-docs', default_options: 'built_subprojects=' + documented_projects)
      message('Gst docs subprojects: ' + documented_projects)
    endif
  else
    message('Not building documentation as hotdoc was not found')
  endif
endif

cmdres = run_command(python3, find_program('scripts/generate_plugins_path.py'), '--builddir',
    meson.build_root(), all_plugins)
assert(cmdres.returncode() == 0, 'Could not create plugins path: @0@'.format(cmdres.stderr()))

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
