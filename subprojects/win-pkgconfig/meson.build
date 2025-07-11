project('win-pkgconfig', version : '0.29.2')

py3 = import('python3').find_python()

if host_machine.system() != 'windows'
  error('Can only download pkgconfig for Windows, sorry')
endif

message('Downloading pkg-config.exe binary for Windows...')

zip_hash = '28fe807d3f9ee06e995388aacf7421b5e3fab08a6e24f3f0d6e9a7921b944bc7'

ret = run_command(py3, files('download-binary.py'), meson.project_version(), zip_hash, check: true)
if ret.returncode() != 0
  message(ret.stdout())
  error(ret.stderr())
endif

meson.override_find_program('pkg-config', find_program('pkg-config.exe'))
