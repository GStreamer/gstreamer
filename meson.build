project('gst-libav', 'c', 'cpp',
  version : '1.9.2.1',
  meson_version : '>= 0.33.0',
  default_options : [ 'warning_level=1',
                      'c_std=gnu99',
                      'buildtype=debugoptimized' ])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
gst_version_major = version_arr[0]
gst_version_minor = version_arr[1]

libavfilter_dep = dependency('libavfilter', version: '>= 6.47.100')
libavformat_dep = dependency('libavformat', version: '>= 57.41.100')
libavcodec_dep = dependency('libavcodec', version: '>= 57.48.101')
libavutil_dep = dependency('libavutil', version: '>= 55.28.100')

libav_deps = [libavfilter_dep, libavformat_dep, libavcodec_dep, libavutil_dep]

cc = meson.get_compiler('c')

check_ffmpeg_src = '''#include <libavcodec/avcodec.h>
#if LIBAVCODEC_VERSION_MICRO >= 100
/* FFmpeg uses 100+ as its micro version */
#else
#error libav provider should be FFmpeg
#endif'''

if not cc.compiles(check_ffmpeg_src, name : 'libav is provided by FFmpeg')
    error('Uncompatible libavcodec found')
endif

cdata = configuration_data()
cdata.set('LIBAV_SOURCE', '"system install"')
cdata.set('PACKAGE_VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE', '"gst-libav"')

gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])

gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])
gstaudio_dep = dependency('gstreamer-audio-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'audio_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
libm = cc.find_library('m', required : false)

configure_file(input : 'config.h.meson',
  output : 'config.h',
  configuration : cdata)

gst_libav_args = ['-DHAVE_CONFIG_H', '-Wno-deprecated-declarations']
configinc = include_directories('.')
plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))
subdir('ext/libav/')
