raw1394_dep = dependency('libraw1394', version: '>= 2.0.0', required: get_option('dv1394'))
avc1394_dep = dependency('libavc1394', version: '>= 0.5.4', required: get_option('dv1394'))
iec61883_dep = dependency('libiec61883', version: '>= 1.0.0', required: get_option('dv1394'))

gst1394_sources = [
  'gst1394.c',
  'gst1394probe.c',
  'gstdv1394src.c',
  'gsthdv1394src.c',
  'gst1394clock.c',
]

gst1394_headers = [
  'gsthdv1394src.h',
  'gst1394clock.h',
  'gstdv1394src.h',
  'gst1394probe.h',
]

doc_sources = []
foreach s: gst1394_sources + gst1394_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  '1394': pathsep.join(doc_sources)
}

have_1394 = false
if raw1394_dep.found() and iec61883_dep.found() and avc1394_dep.found()
  if cc.has_function('avc1394_send_command', dependencies: avc1394_dep)
    if cc.has_function('rom1394_free_directory', dependencies: avc1394_dep)
      have_1394 = true
    endif
  endif
endif

if have_1394
  gst1394 = library('gst1394',
    gst1394_sources,
    c_args : gst_plugins_good_args + ['-DHAVE_LIBIEC61883'],
    include_directories : [configinc, libsinc],
    dependencies : [gstvideo_dep, gstbase_dep, raw1394_dep, avc1394_dep, iec61883_dep],
    install : true,
    install_dir : plugins_install_dir)

  plugins += [gst1394]
endif
