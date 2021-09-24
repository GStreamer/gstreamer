#! /bin/sh

BASE_DIR=$(dirname $0)

OUTDIR=""
if [ $# -eq 1 ]; then
  OUTDIR=$1/
fi

output=$(openssl req -x509 -newkey rsa:4096 -keyout ${OUTDIR}key.pem -out ${OUTDIR}cert.pem -days 365 -nodes -subj "/CN=example.com" 2>&1)

ret=$?
if [ ! $ret -eq 0 ]; then
  echo "${output}" 1>&2
  exit $ret
fi
