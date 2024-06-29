#! /bin/bash

builddir="$1"
tests="$2"

if [[ -z "$builddir" || -z "$tests" ]]; then
  echo "Usage: test.sh <build_directory> <test_name>"
  exit 1
fi

set -eux

timeout="${TIMEOUT_FACTOR:="2"}"
validate="${EXTRA_VALIDATE_ARGS:=""}"
parent="${CI_PROJECT_DIR:-$(pwd)}"

export XDG_RUNTIME_DIR="$(mktemp -p $(pwd) -d xdg-runtime-XXXXXX)"
echo "-> Running $tests"

./gst-env.py \
    "--builddir=$builddir" \
    gst-validate-launcher "$tests" \
    --check-bugs \
    --dump-on-failure \
    --mute \
    --shuffle \
    --no-display \
    --meson-no-rebuild \
    --timeout-factor "$timeout" \
    --fail-on-testlist-change \
    -l "$parent/validate-logs/" \
    --xunit-file "$parent/validate-logs/xunit.xml" \
    $validate
