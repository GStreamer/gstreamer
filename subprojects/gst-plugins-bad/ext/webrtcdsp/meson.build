webrtc_sources = [
  'gstwebrtcdsp.cpp',
  'gstwebrtcechoprobe.cpp',
  'gstwebrtcdspplugin.cpp'
]

webrtc_headers = [
  'gstwebrtcechoprobe.h',
  'gstwebrtcdsp.h',
]

webrtcdsp_opt = get_option('webrtcdsp')
if webrtcdsp_opt.disabled()
  subdir_done()
endif

doc_sources = []
foreach s: webrtc_sources + webrtc_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'webrtcdsp': pathsep.join(doc_sources)
}

# MSVC needs C++20 for designated initializers, and so does a system abseil
# built as C++20 (e.g. GNOME 51)
default_cppstd = 'cpp_std=c++20,c++17'

webrtc_dep = dependency('webrtc-audio-processing-2', version : ['>= 2.0'],
                        required : false)
if not webrtc_dep.found()
  webrtc_dep = dependency('webrtc-audio-processing-1', version : ['>= 1.0'],
                          required : false)
  if webrtc_dep.found()
    cdata.set('HAVE_WEBRTC1', 1)
  endif
endif

if not webrtc_dep.found()
  # Try again, and this time use fallback if requested and possible
  webrtc_dep = dependency('webrtc-audio-processing-2', version : ['>= 2.0'],
                          allow_fallback : true,
                          default_options : [default_cppstd],
                          required : webrtcdsp_opt)
endif

if webrtc_dep.found()
  gstwebrtcdsp = library('gstwebrtcdsp',
    webrtc_sources,
    cpp_args : gst_plugins_bad_args,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gstaudio_dep, gstbadaudio_dep, webrtc_dep],
    install : true,
    install_dir : plugins_install_dir,
    override_options : [default_cppstd],
  )
  plugins += [gstwebrtcdsp]
endif

