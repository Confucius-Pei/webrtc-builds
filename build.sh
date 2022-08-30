#!/bin/bash

set -o errexit
set -o nounset
set -o pipefail

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
source $DIR/util.sh

usage ()
{
cat << EOF

Usage:
   $0 [OPTIONS]

WebRTC automated build script.

OPTIONS:
   -c TARGET CPU  The target cpu for cross-compilation. Default is 'x64'. Other values can be 'x86', 'arm64', 'arm'.
   -n CONFIGS     Build configurations, space-separated. Default is 'Debug Release'. Other values can be 'Debug', 'Release'.
   -d             Debug mode. Print all executed commands.
   -h             Show this message
EOF
}

while getopts :c:n:d OPTION; do
  case $OPTION in
  c) TARGET_CPU=$OPTARG ;;
  n) CONFIGS=$OPTARG ;;
  d) DEBUG=1 ;;
  ?) usage; exit 1 ;;
  esac
done

OUTDIR=${OUTDIR:-out}
SRCDIR=${SRCDIR:-webrtc_src}
ENABLE_RTTI=${ENABLE_RTTI:-1}
DEBUG=${DEBUG:-0}
CONFIGS=${CONFIGS:-Debug Release}
PACKAGE_FILENAME_PATTERN=${PACKAGE_FILENAME_PATTERN:-"webrtc-%tc%"}
DEPOT_TOOLS_DIR=$DIR/depot_tools
TOOLS_DIR=$DIR/tools
PATH=$DEPOT_TOOLS_DIR:$DEPOT_TOOLS_DIR/python276_bin:$PATH

[ "$DEBUG" = 1 ] && set -x

mkdir -p $OUTDIR
OUTDIR=$(cd $OUTDIR && pwd -P)
mkdir -p $SRCDIR
SRCDIR=$(cd $SRCDIR && pwd -P)

TARGET_CPU=${TARGET_CPU:-x64}

echo "SRCDIR: $SRCDIR"
echo "OUTDIR: $OUTDIR"
echo "Target CPU: $TARGET_CPU"

echo Checking build environment dependencies
check::build::env

echo Compiling WebRTC
compile $PLATFORM $OUTDIR "$TARGET_CPU" "$CONFIGS" $SRCDIR

PACKAGE_FILENAME=$(interpret-pattern "$PACKAGE_FILENAME_PATTERN" "$PLATFORM" "$TARGET_CPU")

echo "Packaging WebRTC: $PACKAGE_FILENAME"
package::prepare $PLATFORM $OUTDIR $PACKAGE_FILENAME $DIR/resource "$CONFIGS" $SRCDIR

echo Build successful
