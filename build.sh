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
BRANCH=${BRANCH:-}
BLACKLIST=${BLACKLIST:-}
ENABLE_RTTI=${ENABLE_RTTI:-1}
ENABLE_ITERATOR_DEBUGGING=0
ENABLE_CLANG=0
ENABLE_STATIC_LIBS=1
BUILD_ONLY=${BUILD_ONLY:-1}
DEBUG=${DEBUG:-0}
CONFIGS=${CONFIGS:-Debug Release}
PACKAGE_AS_DEBIAN=${PACKAGE_AS_DEBIAN:-0}
PACKAGE_FILENAME_PATTERN=${PACKAGE_FILENAME_PATTERN:-"webrtc-%p%-%tc%"}
PACKAGE_NAME_PATTERN=${PACKAGE_NAME_PATTERN:-"webrtc"}
DEPOT_TOOLS_DIR=$DIR/depot_tools
TOOLS_DIR=$DIR/tools
PATH=$DEPOT_TOOLS_DIR:$DEPOT_TOOLS_DIR/python276_bin:$PATH

[ "$DEBUG" = 1 ] && set -x

mkdir -p $OUTDIR
OUTDIR=$(cd $OUTDIR && pwd -P)
mkdir -p $SRCDIR
SRCDIR=$(cd $SRCDIR && pwd -P)

detect-platform
TARGET_OS=${TARGET_OS:-$PLATFORM}
TARGET_CPU=${TARGET_CPU:-x64}

echo "BUILD_ONLY: $BUILD_ONLY"
echo "SRCDIR: $SRCDIR"
echo "OUTDIR: $OUTDIR"

echo "Host OS: $PLATFORM"
echo "Target OS: $TARGET_OS"
echo "Target CPU: $TARGET_CPU"

echo Checking build environment dependencies
check::build::env $PLATFORM "$TARGET_CPU"

echo Compiling WebRTC
compile $PLATFORM $OUTDIR "$TARGET_OS" "$TARGET_CPU" "$CONFIGS" "$BLACKLIST" $SRCDIR

PACKAGE_FILENAME=$(interpret-pattern "$PACKAGE_FILENAME_PATTERN" "$PLATFORM" "$TARGET_CPU")

echo "Packaging WebRTC: $PACKAGE_FILENAME"
package::prepare $PLATFORM $OUTDIR $PACKAGE_FILENAME $DIR/resource "$CONFIGS" $SRCDIR

echo Build successful
