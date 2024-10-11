project('win-pkgconfig', version : '0.29.2')

py3 = import('python3').find_python()

if host_machine.system() != 'windows'
  error('Can only download pkgconfig for Windows, sorry')
endif

message('Downloading pkg-config.exe binary for Windows...')

zip_hash = 'b74be141a53f193727fdcb33e9a052a38d9b79c6262326d264e502796b73dfe1'

ret = run_command(py3, files('download-binary.py'), zip_hash, check: true)
if ret.returncode() != 0
  message(ret.stdout())
  error(ret.stderr())
endif

meson.override_find_program('pkg-config', find_program('pkg-config.exe'))
