deps = [ges_dep, gstpbutils_dep, gio_dep]

ges_tool_args = [ges_c_args]
if gstvalidate_dep.found()
  deps = deps + [gstvalidate_dep]
  ges_tool_args += ['-DGST_USE_UNSTABLE_API']
endif

executable('ges-launch-@0@'.format(apiversion),
    'ges-validate.c', 'ges-launch.c', 'ges-launcher.c', 'utils.c',
    c_args : [ges_tool_args],
    dependencies : deps
)
