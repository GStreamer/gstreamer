deps = [ges_dep, gstpbutils_dep, gio_dep, gstvideo_dep, gstaudio_dep]

ges_tool_args = [ges_c_args]
if gstvalidate_dep.found()
  deps = deps + [gstvalidate_dep]
  ges_tool_args += ['-DGST_USE_UNSTABLE_API']
endif

ges_launch = executable('ges-launch-@0@'.format(apiversion),
    'ges-validate.c', 'ges-launch.c', 'ges-launcher.c', 'utils.c', 'ges-launcher-kb.c',
    c_args : [ges_tool_args],
    dependencies : deps,
    install: true
)

install_man('ges-launch-1.0.1')

# bash completion
bashcomp_option = get_option('bash-completion')
bashcomp_dep = dependency('bash-completion', version : '>= 2.0', required : bashcomp_option)
bash_completions_dir = ''
bash_helpers_dir = ''

bashcomp_found = false
if bashcomp_dep.found()
  bashcomp_found = true
  bashcomp_dir_override = bashcomp_dep.version().version_compare('>= 2.10') ? ['datadir', datadir] : ['prefix', prefix]
  bash_completions_dir = bashcomp_dep.get_pkgconfig_variable('completionsdir', define_variable: bashcomp_dir_override)
  if bash_completions_dir == ''
    msg = 'Found bash-completion but the .pc file did not set \'completionsdir\'.'
    if bashcomp_option.enabled()
      error(msg)
    else
      message(msg)
    endif
    bashcomp_found = false
  endif

  bash_helpers_dir = bashcomp_dep.get_pkgconfig_variable('helpersdir', define_variable: bashcomp_dir_override)
  if bash_helpers_dir == ''
    msg = 'Found bash-completion, but the .pc file did not set \'helpersdir\'.'
    if bashcomp_option.enabled()
      error(msg)
    else
      message(msg)
    endif
    bashcomp_found = false
  endif

  if bashcomp_found
    install_data('../data/completions/ges-launch-1.0', install_dir : bash_completions_dir)
  endif
endif
