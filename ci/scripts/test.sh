#! /bin/bash

set -eux

timeout="${TIMEOUT_FACTOR:="2"}"
validate="${EXTRA_VALIDATE_ARGS:=""}"
parent="${CI_PROJECT_DIR:-$(pwd)}"

export XDG_RUNTIME_DIR="$(mktemp -p $(pwd) -d xdg-runtime-XXXXXX)"
echo "-> Running ${TEST_SUITE}"

./gst-env.py \
    gst-validate-launcher ${TEST_SUITE} \
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
