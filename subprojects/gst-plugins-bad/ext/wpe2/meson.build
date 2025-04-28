wpe_sources = [
  'gstwpethreadedview.cpp','gstwpe2.cpp',
  'gstwpevideosrc.cpp',
  'gstwpedisplay.cpp',
  'gstwpeview.cpp',
  'gstwpetoplevel.cpp',
]

wpe_headers = [
  'gstwpevideosrc.h',
  'gstwpe2.h',
  'gstwpethreadedview.h',
]

doc_sources = []
foreach s: wpe_sources + wpe_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'wpe': pathsep.join(doc_sources)
}

wpe_feat = get_option('wpe2').require(gstgl_dep.found(),
  error_message : 'wpe plugin enabled but GL support was not detected')

if not wpe_feat.allowed()
  subdir_done()
endif

wpewebkit_dep = dependency('wpe-webkit-2.0', version: '>=2.46.0', required: wpe_feat)
if not wpewebkit_dep.found()
  subdir_done()
endif

# GstWPE2 makes use of the new WPEPlatform API, as it is optional though we need
# to explicitely check for it.
wpe_platform_dep = dependency('wpe-platform-2.0', required: wpe_feat)
if not wpe_platform_dep.found()
  subdir_done()
endif

egl_dep = dependency('egl', required : wpe_feat)
xkbcommon_dep = dependency('xkbcommon', version : '>= 0.8', required : wpe_feat)

if not (egl_dep.found() and xkbcommon_dep.found())
  subdir_done()
endif

gstwpe = library('gstwpe2',
  wpe_sources,
  override_options : ['cpp_std=c++17'],
  dependencies : [egl_dep, wpewebkit_dep, gstallocators_dep, gstaudio_dep, gstvideo_dep,
    gstbase_dep, gstgl_dep, xkbcommon_dep],
  cpp_args : gst_plugins_bad_args + ['-DHAVE_CONFIG_H=1'],
  include_directories : [configinc],
  install : true,
  install_dir : plugins_install_dir)

plugins += [gstwpe]
