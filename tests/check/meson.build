libparser = library('libparser',
  'elements/parser.c',
  c_args : gst_plugins_good_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc],
  dependencies : [gstcheck_dep],
)

# Only a few so far to sketch the waters.

test('generic/states', executable('states',
  'generic/states.c',
  c_args : gst_plugins_good_args,
  include_directories : [configinc],
  dependencies : [gstcheck_dep],)
)

# FIXME, set up registry and envvars
test('alphacolor',
  executable('alphacolor', 'elements/alphacolor.c',
    dependencies : [gstcheck_dep, gstvideo_dep])
)
