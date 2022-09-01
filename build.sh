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

while getopts :c:n OPTION; do
  case $OPTION in
  c) TARGET_CPU=$OPTARG ;;
  n) CONFIGS=$OPTARG ;;
  ?) usage; exit 1 ;;
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

echo Checking build environment dependencies
check::build::env

echo Compiling WebRTC
compile "$TARGET_CPU" "$CONFIGS"

echo "Packaging WebRTC"
package::prepare $OUTDIR webrtc "$CONFIGS" $SRCDIR

echo Build successful
