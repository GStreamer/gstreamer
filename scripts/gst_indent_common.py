import shutil
import subprocess


def indent(*args):
    indent = shutil.which('gst-indent-1.0')

    if not indent:
        raise RuntimeError('''Did not find gst-indent-1.0, please install it before continuing.''')

    version = subprocess.run([indent, '--version'], capture_output=True, text=True)

    if 'gst-indent' not in version.stdout:
        raise RuntimeError(f'''Did not find gst-indent-1.0, please install it before continuing.
      (Found {indent}, but it doesn't seem to be gst-indent-1.0)''')

    subprocess.check_call([indent] + list(args))
