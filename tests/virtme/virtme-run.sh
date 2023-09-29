#!/bin/bash
#
# Based on the virtme-run.sh script from the Mutter project:
# https://gitlab.gnome.org/GNOME/mutter/-/blob/main/src/tests/kvm/virtme-run.sh
#
# Run fluster tests in a virtual machine using virtme-ng.
#
# $1: A Linux kernel image
# $2: The test build dir
# $3: GStreamer source dir
# $4: The decoder to be run in [vp8, vp9, h.264, h.265, av1]
# ($@: The test vectors to be skipped)

set -e

DIRNAME="$(dirname "$0")"
IMAGE="$1"
MESON_BUILD_DIR="$2"
MESON_SOURCE_DIR="$3"
DECODER="${4}"

shift
shift
shift
shift

SKIPPED="$@"

if [ ! -z "${SKIPPED}" ]; then
	sv="-sv ${SKIPPED}"
fi

TEST_RESULT_FILE=$(mktemp -p "$MESON_BUILD_DIR" -t test-result-XXXXXX)
echo 1 > "$TEST_RESULT_FILE"

VIRTME_ENV="\
MESON_BUILD_DIR=${MESON_BUILD_DIR} \
"

TEST_SUITES_DIR="${MESON_SOURCE_DIR}/ci/fluster/visl_references"

FLUSTER_PATH=/opt/fluster
TEST_COMMAND="${FLUSTER_PATH}/fluster.py -tsd ${TEST_SUITES_DIR} run -d GStreamer-${DECODER}-V4L2SL-Gst1.0 -f junitxml -so $MESON_BUILD_DIR/fluster-results-${DECODER}.xml ${sv} -t 60"

SCRIPT="\
  env $VIRTME_ENV $DIRNAME/run-virt-test.sh \
  \\\"$TEST_COMMAND\\\" \
  \\\"$TEST_RESULT_FILE\\\" \
"

HALF_MEMORY="$(grep MemTotal /proc/meminfo | awk '{print $2}' | xargs -I {} echo "scale=0; 1+{}/1024^2/2" | bc)G"

echo Running tests in virtual machine ...
virtme-run \
  --memory=${HALF_MEMORY} \
  --rw \
  --pwd \
  --kimg "$IMAGE" \
  --script-sh "sh -c \"$SCRIPT\"" \
  -a visl.stable_output=true \
  -a visl.codec_variability=true \
  --show-boot-console --show-command \
  --qemu-opts -cpu host,pdcm=off -smp 8
VM_RESULT=$?
if [ $VM_RESULT != 0 ]; then
  echo Virtual machine exited with a failure: $VM_RESULT
else
  echo Virtual machine terminated.
fi
TEST_RESULT="$(cat "$TEST_RESULT_FILE")"

echo Test result exit status: $TEST_RESULT

rm "$TEST_RESULT_FILE"
exit "$TEST_RESULT"
