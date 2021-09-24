library('gstvalidate-default-overrides-1.0',
    sources: 'gst-validate-default-overrides.c',
    version : libversion,
    soversion : soversion,
    darwin_versions : osxversion,
    include_directories : [inc_dirs],
    install: true,
    c_args : [gst_c_args] + ['-D_GNU_SOURCE'],
    dependencies : validate_dep)
