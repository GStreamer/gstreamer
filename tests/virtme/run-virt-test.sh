#!/bin/sh
#
# Run the given command in the meson dev environment.
# The command return value will be stored in the given
# status file.
#
# $1: The command to be run
# $2: The status file

set -e

COMMAND="${1}"
STATUS_FILE="${2}"

echo Run ${COMMAND} in the devenv
meson devenv -C ${MESON_BUILD_DIR} ${COMMAND}

STATUS=$?
echo $STATUS > ${STATUS_FILE}

exit $STATUS
