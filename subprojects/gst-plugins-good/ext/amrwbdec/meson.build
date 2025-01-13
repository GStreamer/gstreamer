amrwb_dep = dependency('opencore-amrwb', version : '>= 0.1.3', required : get_option('amrwbdec'))

amrwbdec_sources = [
  'amrwb.c',
  'amrwbdec.c',
]

amrwbdec_headers = [
  'amrwbdec.h',
]

doc_sources = []
foreach s: amrwbdec_sources + amrwbdec_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'amrwbdec': pathsep.join(doc_sources)
}

if amrwb_dep.found()
  amrwbdec = library('gstamrwbdec',
    amrwbdec_sources,
    c_args : gst_plugins_good_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, amrwb_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [amrwbdec]
endif
