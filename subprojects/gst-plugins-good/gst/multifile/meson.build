multifile_sources = [
  'gstmultifilesink.c',
  'gstmultifilesrc.c',
  'gstmultifile.c',
  'gstsplitfilesrc.c',
  'gstsplitmuxpartreader.c',
  'gstsplitmuxsink.c',
  'gstsplitmuxsrc.c',
  'gstsplitutils.c',
  'location-utils.c',
  'patternspec.c',
  'gstimagesequencesrc.c',
]

multifile_headers = [
  'gstimagesequencesrc.h',
  'gstmultifilesink.h',
  'gstmultifilesrc.h',
  'gstsplitfilesrc.h',
  'gstsplitmuxpartreader.h',
  'gstsplitmuxsink.h',
  'gstsplitmuxsrc.h',
  'gstsplitutils.h',
  'location-utils.h',
  'patternspec.h',
]

doc_sources = []
foreach s: multifile_sources + multifile_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'multifile': pathsep.join(doc_sources)
}

if get_option('multifile').disabled()
  subdir_done()
endif

gstmultifile = library('gstmultifile',
  multifile_sources + multifile_headers,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstvideo_dep, gstbase_dep,
                  gstpbutils_dep, gio_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstmultifile]

if get_option('tests').disabled() or static_build
  subdir_done()
endif

test_splitmuxpartreader_sources = [
  'test-splitmuxpartreader.c',
  'gstsplitmuxpartreader.c',
  'gstsplitmuxsrc.c',
  'gstsplitutils.c',
  'patternspec.c',
]

executable('test-splitmuxpartreader',
  test_splitmuxpartreader_sources,
  c_args : gst_plugins_good_args,
  include_directories : [configinc, libsinc],
  dependencies : [gsttag_dep, gstbase_dep],
  install : false,
)
