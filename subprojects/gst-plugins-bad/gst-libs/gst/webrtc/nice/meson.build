libgstwebrtcnice_sources = files([
    'nice.c',
    'nicestream.c',
    'nicetransport.c',
])

libgstwebrtcnice_headers = files([
    'nice_fwd.h',
    'nice.h',
    'nicestream.h',
    'nicetransport.h',
])

libnice_dep = dependency('nice', version : '>=0.1.17', required : get_option('webrtc'),
                         fallback : ['libnice', 'libnice_dep'],
                         default_options: ['tests=disabled'])

deps = [gstwebrtc_dep, libnice_dep]

if libnice_dep.found()
  libnice_version = libnice_dep.version()
  libnice_c_args = []
  if libnice_version.version_compare('<0.1.20') or libnice_version.version_compare('<0.1.19.1')
    version_arr = libnice_version.split('.')
    libnice_version_major = version_arr[0]
    libnice_version_minor = version_arr[1]
    libnice_version_micro = version_arr[2]
    if version_arr.length() == 4
      libnice_version_nano = version_arr[3]
    else
      libnice_version_nano = '0'
    endif
    libnice_c_args = ['-DNICE_VERSION_MAJOR=' + libnice_version_major,
                      '-DNICE_VERSION_MINOR=' + libnice_version_minor,
                      '-DNICE_VERSION_MICRO=' + libnice_version_micro,
                      '-DNICE_VERSION_NANO=' + libnice_version_nano ]
  endif
  libgstwebrtcnice = library('gstwebrtcnice-' + api_version,
    libgstwebrtcnice_sources, libgstwebrtcnice_headers,
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DBUILDING_GST_WEBRTCNICE', '-DG_LOG_DOMAIN="GStreamer-webrtcnice"'] + libnice_c_args,
    include_directories: [configinc],
    version : libversion,
    soversion : soversion,
    darwin_versions : osxversion,
    dependencies: deps,
    install: true,
  )
  
  pkg_name = 'gstreamer-webrtc-nice-1.0'
  libraries += [[pkg_name, {'lib': libgstwebrtcnice}]]
  pkgconfig.generate(libgstwebrtcnice,
    libraries : [deps],
    variables : pkgconfig_variables,
    subdirs : pkgconfig_subdirs,
    name : pkg_name,
    description : 'libnice based implementaion for GstWebRTCICE',
  )

  libgstwebrtcnice_dep = declare_dependency(link_with: libgstwebrtcnice,
    dependencies: [deps])

  install_headers(libgstwebrtcnice_headers, subdir : 'gstreamer-1.0/gst/webrtc/nice')
  meson.override_dependency(pkg_name, libgstwebrtcnice_dep)
endif
