#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source $DIR/util.sh

while getopts :c:n OPTION; do
   case $OPTION in
   c) TARGET_CPU=$OPTARG ;;
   n) CONFIGS=$OPTARG ;;
   ?)
      usage
      exit 1
      ;;
   esac
done

DEBUG=${DEBUG:-0}
CONFIGS=${CONFIGS:-Debug Release}
DEPOT_TOOLS_DIR=$DIR/depot_tools
TOOLS_DIR=$DIR/tools
PATH=$DEPOT_TOOLS_DIR:$DEPOT_TOOLS_DIR/python276_bin:$PATH

set -x

OUTDIR=out
mkdir -p $OUTDIR
OUTDIR=$(cd $OUTDIR && pwd -P)

SRCDIR=webrtc_src
mkdir -p $SRCDIR
SRCDIR=$(cd $SRCDIR && pwd -P)

TARGET_CPU=${TARGET_CPU:-x64}

echo "Target CPU: $TARGET_CPU"

#Checking build environment dependencies
echo Checking build environment dependencies
check::build::env

#Compiling WebRTC
echo Compiling WebRTC
compile $SRCDIR $OUTDIR "$TARGET_CPU" "$CONFIGS"

#Packaging WebRTC
echo Packaging WebRTC
package::package $SRCDIR $OUTDIR "$CONFIGS"

echo Build successful
