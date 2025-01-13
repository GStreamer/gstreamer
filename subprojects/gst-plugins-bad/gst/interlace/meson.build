interlace_sources = [
  'gstinterlace.c',
]

interlace_headers = [
]

doc_sources = []
foreach s: interlace_sources + interlace_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'interlace': pathsep.join(doc_sources)
}

if get_option('interlace').disabled()
  subdir_done()
endif

gstinterlace = library('gstinterlace',
  interlace_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstinterlace]
