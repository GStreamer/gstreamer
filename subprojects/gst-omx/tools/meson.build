gst_tools = {
  'listcomponents': {
    'files': files('listcomponents.c'),
    'include_directories' : [configinc, omx_inc],
    'deps': [gmodule_dep],
    'extra_c_args': gst_omx_args + extra_c_args,
    'install': false,
  },
}

if not get_option('tools').disabled() and not static_build
  foreach tool, data: gst_tools
    exe_name = '@0@-@1@'.format(tool, api_version)
    executable(exe_name,
      data.get('files'),
      install: data.get('install', true),
      install_tag: 'bin',
      include_directories : data.get('include_directories', [configinc]),
      dependencies : data.get('deps', []),
      c_args: data.get('extra_c_args', []),
    )

    if data.has_key('man_page')
      install_man(data.get('man_page'))
    endif
  endforeach
endif
