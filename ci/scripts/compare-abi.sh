#! /bin/bash

set -eux

old_abi=${1}
output_dir=${2}
output_fail_file=${3}
upstream_branch=${GST_UPSTREAM_BRANCH:-}

module=$(basename ${old_abi})

opts="--drop-private-types"
if [ "x$upstream_branch" = "xmain" ]
then
  # don't error out on added symbols
  opts="${opts} --no-added-syms"
fi

mkdir -p ${output_dir}
if ! abidiff ${opts} ${old_abi} ${output_dir}/${module}
then
  echo ${module} >> ${output_fail_file}
  exit 1;
fi
