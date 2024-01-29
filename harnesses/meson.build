if get_option('harnesses').disabled()
  subdir_done()
endif

harnesses = [
  ['test_jpegdec.c']
]

glib_req = '>= 2.64.0'
gst_req = '>= ' + meson.project_version()

extra_sources = []
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_base_dep'])
gstnet_dep = dependency('gstreamer-net-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_net_dep'])
#gstcontroller_dep = dependency('gstreamer-controller-1.0', version : gst_req,
#  fallback : ['gstreamer', 'gst_controller_dep'])
gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_check_dep'])
gstpbutils_dep = dependency('gstreamer-pbutils-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'pbutils_dep'])
#gstallocators_dep = dependency('gstreamer-allocators-1.0', version : gst_req,
#    fallback : ['gst-plugins-base', 'allocators_dep'])
gstapp_dep = dependency('gstreamer-app-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'app_dep'])
#gstaudio_dep = dependency('gstreamer-audio-1.0', version : gst_req,
#    fallback : ['gst-plugins-base', 'audio_dep'])
gstfft_dep = dependency('gstreamer-fft-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'fft_dep'])
gstriff_dep = dependency('gstreamer-riff-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'riff_dep'])
gstrtp_dep = dependency('gstreamer-rtp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'rtp_dep'])
gstrtsp_dep = dependency('gstreamer-rtsp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'rtsp_dep'])
gstsdp_dep = dependency('gstreamer-sdp-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'sdp_dep'])
gsttag_dep = dependency('gstreamer-tag-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'tag_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
    fallback : ['gst-plugins-base', 'video_dep'])
gio_dep = dependency('gio-2.0', version: glib_req)

common_deps = [gst_dep, gst_dep, gstbase_dep, gstnet_dep, gstcheck_dep,
  gstvideo_dep, gstpbutils_dep, gstrtp_dep, gstrtsp_dep, gsttag_dep,
  gstapp_dep, gio_dep]

#cxx = meson.get_compiler('cpp')
#fuzzing_engine = cxx.find_library('FuzzingEngine', required: true)

#libsoup2_dep = dependency('libsoup-2.4', version : '>=2.48',
#                          required : false, fallback : ['libsoup', 'libsoup_dep'],
#                          default_options: ['sysprof=disabled'])
#libsoup3_dep = dependency('libsoup-3.0', required : false,
#                           fallback : ['libsoup3', 'libsoup_dep'])

pluginsdirs = []
if gst_dep.type_name() == 'pkgconfig'
  pbase = dependency('gstreamer-plugins-base-' + api_version, required: true)
  pluginsdirs = [gst_dep.get_variable('pluginsdir'),
                 pbase.get_variable('pluginsdir')]
  gst_plugin_scanner_dir = gst_dep.get_variable('pluginscannerdir')
else
  gst_plugin_scanner_dir = subproject('gstreamer').get_variable('gst_scanner_dir')
endif
gst_plugin_scanner_path = join_paths(gst_plugin_scanner_dir, 'gst-plugin-scanner')

# fake device drivers: we could run hardware element tests against dummy drivers

foreach harness : harnesses
  file_name = harness.get(0)
  test_name = file_name.split('.').get(0)

  extra_deps = []
  if harness.length() >= 3
    extra_deps = dependency(target.get(2))
  endif

  env = environment()
  env.set('GST_PLUGIN_SYSTEM_PATH_1_0', '')
  env.set('GST_PLUGIN_PATH_1_0', [meson.global_build_root()] + pluginsdirs)
  env.set('GSETTINGS_BACKEND', 'memory')
  env.set('GST_REGISTRY', join_paths(meson.current_build_dir(), '@0@.registry'.format(test_name)))
  env.set('GST_PLUGIN_SCANNER_1_0', gst_plugin_scanner_path)

  exe = executable(test_name, [extra_sources, file_name],
    dependencies: common_deps + extra_deps,
    c_args: ['-fsanitize=fuzzer,address'],
    link_args: ['-fsanitize=fuzzer,address']
  )
endforeach
