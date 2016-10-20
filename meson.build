project('All GStreamer modules', 'c', version : '1.9.2.1')

gst_version = '>= @0@'.format(meson.project_version())
gst_branch = 'master'

glib_req = '>= 2.40.0'

subprojects = [
    'gstreamer',
    'gst-plugins-base',
    'gst-plugins-good',
    'gst-plugins-ugly',
    'gst-plugins-bad',
    'gst-devtools',
    'gst-editing-services'
]

# FIXME Remove that check once we have ffmpeg as a gst-libav subproject
libavfilter_dep = dependency('libavfilter', version: '>= 6.47.100', required: false)
gst_libav = []
if libavfilter_dep.found()
    cc = meson.get_compiler('c')
    check_ffmpeg_src = '''#include <libavcodec/avcodec.h>
    #if LIBAVCODEC_VERSION_MICRO >= 100
    /* FFmpeg uses 100+ as its micro version */
    #else
    #error libav provider should be FFmpeg
    #endif'''
    if cc.compiles(check_ffmpeg_src, name : 'whether libav is provided by FFmpeg')
        gst_libav = ['gst-libav']
    endif
endif

if gst_libav.length() == 0
    message('WARNING: gst-libav not built as ffmpeg >= n3.1.2 not found on the system')
else
    subprojects += gst_libav
endif

if get_option('enable_python')
    subprojects += ['gst-python']
endif

foreach subproj: subprojects
    subproject(subproj, version: gst_version)
endforeach

setenv = find_program('gst-uninstalled.py')
run_target('uninstalled', command : [setenv, '--builddir=@0@'.format(meson.current_build_dir()),
           '--gst-version=@0@'.format(gst_branch)])
