subparse_sources = [
  'gstssaparse.c',
  'gstsubparse.c',
  'gstsubparseelement.c',
  'gstsubparseplugin.c',
  'samiparse.c',
  'tmplayerparse.c',
  'mpl2parse.c',
  'qttextparse.c',
]

subparse_headers = [
  'qttextparse.h',
  'samiparse.h',
  'gstsubparseelements.h',
  'gstssaparse.h',
  'mpl2parse.h',
  'tmplayerparse.h',
  'gstsubparse.h',
]

doc_sources = []
foreach s: subparse_sources + subparse_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'subparse': pathsep.join(doc_sources)
}

if get_option('subparse').disabled()
  subdir_done()
endif

gstsubparse = library('gstsubparse',
  subparse_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  dependencies : [gst_base_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstsubparse]
