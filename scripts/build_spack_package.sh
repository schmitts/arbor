#!/usr/bin/env bash

set -Eeuo pipefail

trap cleanup SIGINT SIGTERM ERR EXIT

cleanup() {
  trap - SIGINT SIGTERM ERR EXIT
  rm -rf "$TMP_DIR"
}

TMP_DIR=$(mktemp -d)
ARBOR_SOURCE=$1

ARBOR_DIR=$TMP_DIR/arbor
mkdir $ARBOR_DIR
cp -r $ARBOR_SOURCE/* $ARBOR_DIR

SPACK_CUSTOM_REPO=custom_repo
spack repo create $SPACK_CUSTOM_REPO

mkdir -p $SPACK_CUSTOM_REPO/packages/arbor
spack repo add $SPACK_CUSTOM_REPO

cp $ARBOR_DIR/spack/package.py $SPACK_CUSTOM_REPO/packages/arbor
cd $ARBOR_DIR
#spack dev-build arbor@with-package-from-repo

spack install libsigsegv

