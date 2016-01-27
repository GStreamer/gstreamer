project('gst-rtsp-server', 'c',
  version : '1.9.1.1',
  meson_version : '>= 0.33.0',
  default_options : ['warning_level=1', 'buildtype=debugoptimized'])

gst_version = meson.project_version()
version_arr = gst_version.split('.')
gst_version_major = version_arr[0]
gst_version_minor = version_arr[1]
gst_version_micro = version_arr[2]
if version_arr.length() == 4
  gst_version_nano = version_arr[3]
else
  gst_version_nano = 0
endif

glib_req = '>= 2.40.0'
gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)

api_version = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
libversion = '@0@.@1@.0'.format(soversion, gst_version_minor.to_int() * 100 + gst_version_micro.to_int())

plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

cdata = configuration_data()
cdata.set('GETTEXT_PACKAGE', '"gst-rtsp-server-1.0"')
cdata.set('PACKAGE', '"gst-rtsp-server"')
cdata.set('VERSION', '"@0@"'.format(gst_version))
cdata.set('PACKAGE_VERSION', '"@0@"'.format(gst_version))
cdata.set('GST_PACKAGE_NAME', '"GStreamer RTSP Server Library"')
cdata.set('GST_PACKAGE_ORIGIN', '"Unknown package origin"')
#cdata.set('GST_API_VERSION', '"@0@"'.format(api_version))
cdata.set('GST_LICENSE', '"LGPL"')

configure_file(input : 'config.h.meson',
  output : 'config.h',
  configuration : cdata)

rtspserver_args = ['-DHAVE_CONFIG_H']

rtspserver_incs = include_directories('gst/rtsp-server', '.')

glib_dep = dependency('glib-2.0', version : glib_req)
gst_dep = dependency('gstreamer-1.0', version : gst_req)
gstcheck_dep = dependency('gstreamer-check-1.0', version : gst_req)
gstrtsp_dep = dependency('gstreamer-rtsp-1.0', version : gst_req)
gstrtp_dep = dependency('gstreamer-rtp-1.0', version : gst_req)
gstsdp_dep = dependency('gstreamer-sdp-1.0', version : gst_req)
gstapp_dep = dependency('gstreamer-app-1.0', version : gst_req)
gstnet_dep = dependency('gstreamer-net-1.0', version : gst_req)

subdir('gst')
subdir('tests')
subdir('examples')
subdir('pkgconfig')
