nle_sources = ['nleobject.c',
    'nlecomposition.c',
    'nleghostpad.c',
    'nleoperation.c',
    'nlesource.c',
    'nleurisource.c',
    'gstnle.c'
]

nle = library('gstnle', nle_sources,
  dependencies : [gst_dep, gstbase_dep],
  include_directories: [configinc],
  c_args : ges_c_args,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [nle]
