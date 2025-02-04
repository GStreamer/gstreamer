import shutil
import subprocess


def indent(*args):
    indent = shutil.which('gst-indent-1.0')

    if not indent:
        raise RuntimeError('''Did not find gst-indent-1.0, please install it before continuing.''')

    version = subprocess.run([indent, '--version'], capture_output=True, text=True)

    if 'GNU' not in version.stdout:
        raise RuntimeError(f'''Did not find gst-indent-1.0, please install it before continuing.
      (Found {indent}, but it doesn't seem to be gst-indent-1.0)''')

    # Run twice. GNU indent isn't idempotent
    # when run once
    for i in range(2):
        subprocess.check_call([indent,
            '--braces-on-if-line',
            '--case-brace-indentation0',
            '--case-indentation2',
            '--braces-after-struct-decl-line',
            '--line-length80',
            '--no-tabs',
            '--cuddle-else',
            '--dont-line-up-parentheses',
            '--continuation-indentation4',
            '--honour-newlines',
            '--tab-size8',
            '--indent-level2',
            '--leave-preprocessor-space'] + list(args)
        )
