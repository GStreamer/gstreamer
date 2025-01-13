nle_sources = ['nleobject.c',
    'nlecomposition.c',
    'nleghostpad.c',
    'nleoperation.c',
    'nlesource.c',
    'nleurisource.c',
    'gstnle.c'
]

nle_headers = [
  'nlesource.h',
  'nle.h',
  'nleurisource.h',
  'nletypes.h',
  'nlecomposition.h',
  'nleghostpad.h',
  'nleobject.h',
  'nleoperation.h',
]

deps = [gst_dep, gstbase_dep]
c_args = ges_c_args
if gstvalidate_dep.found()
  deps += [gstvalidate_dep]
  nle_sources += ['validate.c']
endif


nle = library('gstnle', nle_sources,
  dependencies : deps,
  include_directories: [configinc],
  c_args : c_args,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [nle]

doc_sources = []
foreach s: nle_sources + nle_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'nle': pathsep.join(doc_sources)
}