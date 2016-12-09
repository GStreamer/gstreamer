libcheck_files = [
  'check.c',
  'check_error.c',
  'check_list.c',
  'check_log.c',
  'check_msg.c',
  'check_pack.c',
  'check_print.c',
  'check_run.c',
  'check_str.c',
  'libcompat/libcompat.c'
]

if not cdata.has('HAVE_ALARM')
  libcheck_files += ['libcompat/alarm.c']
endif

if not cdata.has('HAVE_CLOCK_GETTIME')
  libcheck_files += ['libcompat/clock_gettime.c']
endif

if not cdata.has('HAVE_DECL_STRSIGNAL')
  libcheck_files += ['libcompat/strsignal.c']
endif

# FIXME: check for symbols timer_create, timer_settime, timer_delete as well
if not rt_lib.found()
  libcheck_files += [
    'libcompat/timer_create.c',
    'libcompat/timer_settime.c',
    'libcompat/timer_delete.c'
  ]
endif

configure_file(input : 'check.h.in',
  output : 'check.h',
  configuration : check_cdata)

internal_check_h_inc = include_directories('..')

libcheck = static_library('check',
  libcheck_files,
  include_directories : [configinc, internal_check_h_inc],
  dependencies : [rt_lib, mathlib],
  c_args: gst_c_args,
  pic: true)
