project('win-nasm', version : '2.14.02')

py3 = import('python3').find_python()

if host_machine.system() != 'windows'
  error('Can only download nasm for Windows, sorry')
endif

message('Downloading and extracting nasm binaries for Windows...')

arch = host_machine.cpu_family()
if arch == 'x86_64'
  zip_hash = '18918ac906e29417b936466e7a2517068206c8db8c04b9762a5befa18bfea5f0'
else
  zip_hash = '250f9b5eeb2111e8c7b494a977490985b8604fe7518a6f5041cde37cc727a067'
endif

ret = run_command(py3, files('download-binary.py'), meson.project_version(), arch, zip_hash)
if ret.returncode() != 0
  message(ret.stdout())
  error(ret.stderr())
endif

meson.override_find_program('nasm', find_program(join_paths('nasm-@0@'.format(meson.project_version()), 'nasm')))
