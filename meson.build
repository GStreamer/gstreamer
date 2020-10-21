project('All GStreamer modules', 'c',
  version : '1.16.3',
  meson_version : '>= 0.48.0',
  default_options : ['buildtype=debugoptimized'])

gst_version = '>= @0@'.format(meson.project_version())
gst_branch = 'master'

glib_req = '>= 2.40.0'

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
endif

# Ordered list of subprojects (dict has no ordering guarantees)
subprojects = [
  ['gstreamer', {}],
  ['gst-plugins-base', {}],
  ['gst-plugins-good', {}],
  ['libnice', { 'option': get_option('libnice'), 'match_gst_version': false}],
  ['gst-plugins-bad', { 'option': get_option('bad') }],
  ['gst-plugins-ugly', { 'option': get_option('ugly') }],
  ['gst-libav', { 'option': get_option('libav') }],
  ['gst-rtsp-server', { 'option': get_option('rtsp_server') }],
  ['gst-devtools', { 'option': get_option('devtools')}],
  ['gst-integration-testsuites', { 'option': get_option('devtools') }],
  ['gst-editing-services', { 'option': get_option('ges') }],
  ['gstreamer-vaapi', { 'option': get_option('vaapi') }],
  ['gst-omx', { 'option': get_option('omx'), }],
  ['gstreamer-sharp', { 'option': get_option('sharp'), 'match_gst_version': false }],
  ['pygobject', { 'option': get_option('python'), 'match_gst_version': false }],
  ['gst-python', { 'option': get_option('python') }],
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

subproject('orc', required: get_option('orc'))

subprojects_names = []
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
    subprojects_names += [project_name]
    cmdres = run_command(python3, '-c', symlink.format(project_name, meson.current_source_dir()))
    if cmdres.returncode() == 0
        message('Created symlink to ' + project_name)
    endif
  endif
endforeach

foreach custom_subproj: get_option('custom_subprojects').split(',')
    if custom_subproj != ''
        message ('Adding custom subproject ' + custom_subproj)
        subproject(custom_subproj)
        subprojects_names += [custom_subproj]
    endif
endforeach

message('Building subprojects: ' + ', '.join(subprojects_names))

setenv = find_program('gst-uninstalled.py')
run_target('uninstalled', command : [setenv, '--builddir=@0@'.format(meson.build_root()),
	   '--srcdir=@0@'.format(meson.source_root())])

update = find_program('git-update')
run_target('git-update', command : [update])
run_target('update', command : [update,
    '--builddir=@0@'.format(meson.current_build_dir())])
