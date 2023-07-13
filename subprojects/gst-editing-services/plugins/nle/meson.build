nle_sources = ['nleobject.c',
    'nlecomposition.c',
    'nleghostpad.c',
    'nleoperation.c',
    'nlesource.c',
    'nleurisource.c',
    'gstnle.c'
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
