project('win-flex-bison-binary', version : '2.5.14')

provide_flex = not find_program('flex', required : false).found()
provide_bison = not find_program('bison', required : false).found()

if provide_flex or provide_bison
  py3 = import('python3').find_python()

  message('Downloading and extracting win-flex-bison binaries...')

  zip_hash = '354c9aae02aca421c52abfda7fe3ce6c32ad07e25ff3f66e31da9437a0b906cf'

  ret = run_command(py3, files('download-binary.py'), meson.project_version(), zip_hash)
  if ret.returncode() != 0
    message(ret.stdout())
    error(ret.stderr())
  endif

  if provide_flex
    meson.override_find_program('flex', find_program('win_flex'))
  endif
  if provide_bison
    meson.override_find_program('bison', find_program('win_bison'))
  endif
endif
