project('All GStreamer modules', 'c',
  version : '1.15.0.1',
  meson_version : '>= 0.47.0',
  default_options : ['buildtype=debugoptimized'])

gst_version = '>= @0@'.format(meson.project_version())
gst_branch = 'master'

glib_req = '>= 2.40.0'

build_system = build_machine.system()
cc = meson.get_compiler('c')

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

libav_opt = get_option('libav')
libav_deps = [dependency('libavfilter', version: '>= 7.16.100',
              fallback: ['FFmpeg', 'libavfilter_dep'], required: libav_opt)]
if libav_deps[0].found() and libav_deps[0].type_name() != 'internal'
  cc = meson.get_compiler('c')
  check_ffmpeg_src = '''#include <libavcodec/avcodec.h>
  #if LIBAVCODEC_VERSION_MICRO >= 100
  /* FFmpeg uses 100+ as its micro version */
  #else
  #error libav provider should be FFmpeg
  #endif'''
  if not cc.compiles(check_ffmpeg_src, dependencies : libav_deps, name : 'whether libav is provided by FFmpeg')
    message('WARNING: gst-libav not built as ffmpeg n3.1.2 not found on the system')
    libav_deps += [disabler()]
  endif
endif

if add_languages('cs', required : get_option('sharp'))
  sharp_deps = []
else
  sharp_deps = [disabler()]
endif

python_opt = get_option('python')
vaapi_opt = get_option('vaapi')
devtools_opt = get_option('devtools')
ges_opt = get_option('ges')
subprojects = {
    'gstreamer': {},
    'gst-plugins-base': {},
    'gst-plugins-good': {},
    'gst-plugins-bad': { 'option': get_option('bad') },
    'gst-plugins-ugly': { 'option': get_option('ugly') },
    'gst-python': {
      'option': python_opt,
      'dependencies': [dependency('pygobject-3.0', fallback: ['pygobject', 'pygobject_dep'], required : python_opt)]
    },
    'gst-omx': { 'option': get_option('omx'), },
    'gst-libav': { 'option': get_option('libav'), 'dependencies': libav_deps},
    'gstreamer-vaapi': {
      'option': vaapi_opt,
      'dependencies': [dependency('libva', version: ['>= 0.30.4', '!= 0.99.0'], required : vaapi_opt)]
    },
    'gst-devtools': {
      'option': devtools_opt
    },
    'gst-editing-services': { 'option': ges_opt, 'dependencies': [dependency('libxml-2.0', required : ges_opt)] },
    'gst-rtsp-server': { 'option': get_option('rtsp_server') },
    'gstreamer-sharp': { 'option': get_option('sharp'), 'dependencies': sharp_deps },
}

python3 = import('python3').find_python()
symlink = '''
import os

os.symlink(os.path.join('@1@', 'subprojects', '@0@'),
  os.path.join('@1@', '@0@'))
'''

# On Windows, if flex/bison aren't found, we use a subproject to get them
flex = find_program('flex', 'win_flex', required : build_system != 'windows')
bison = find_program('bison', 'win_bison', required : build_system != 'windows')
if not flex.found() or not bison.found()
  subproject('win-flex-bison-binaries')
endif

subprojects_names = []
foreach project_name, build_infos: subprojects
    build = true
    if build_infos.has_key('option')
      build = not build_infos.get('option').disabled()
      if build
        foreach dep: build_infos.get('dependencies', [])
          if dep.found() == false
            warning('@0@ dependency @1@ not found - NOT BUILDING'.format(project_name, dep))
            build = false
          endif
        endforeach
      endif
    endif

    if build
      subprojects_names += [project_name]
      subproject(project_name, version: gst_version)
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
