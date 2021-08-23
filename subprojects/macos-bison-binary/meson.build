project('win-flex-bison-binary', version : '3.7.6')

py3 = import('python3').find_python()

message('Downloading and extracting bison for macOS x64...')

arch = host_machine.cpu_family()
tar_hash = '932f91d7c7fa0121abc3e5f8e54a7234b03d3de468c254ab8063ff8e6eb92a09'
if arch != 'x86_64'
  warning('bison binary is untested on non-x86_64, please report whether this worked or not')
  arch = 'x86_64'
endif

ret = run_command(py3, files('download-binary.py'), meson.project_version(), arch, tar_hash)
if ret.returncode() != 0
  message(ret.stdout())
  error(ret.stderr())
endif

conf = configuration_data()
conf.set('SRCDIR', meson.project_source_root())
conf.set('EXTRACTDIR', ret.stdout())
bison_py = configure_file(
    input: 'bison.py.in',
    output: 'bison.py',
    configuration: conf)

meson.override_find_program('bison', find_program(bison_py))
