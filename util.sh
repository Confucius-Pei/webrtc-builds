# Check if any of the arguments is executable (logical OR condition).
# Using plain "type" without any option because has-binary is intended
# to know if there is a program that one can call regardless if it is
# an alias, builtin, function, or a disk file that would be executed.
function has-binary() {
  type "$1" &>/dev/null
}

# Setup Visual Studio build environment variables.
function init-msenv() {

  # Rudimentary support for VS2017 in default install location due to
  # lack of VS1S0COMNTOOLS environment variable.
  if [ -d "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/Common7/Tools" ]; then
    VsDevCmd_path="C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/Common7/Tools"
  else
    echo "Building under Microsoft Windows requires Microsoft Visual Studio 2019"
    exit 1
  fi

  export DEPOT_TOOLS_WIN_TOOLCHAIN=0
  pushd "$VsDevCmd_path" >/dev/null
  OLDIFS=$IFS
  IFS=$'\n'
  msvars=$(cmd //c "VsDevCmd.bat && set")

  for line in $msvars; do
    case $line in
    INCLUDE=* | LIB=* | LIBPATH=*)
      export $line
      ;;
    PATH=*)
      PATH=$(
        echo $line | sed \
        -e 's/PATH=//' \
        -e 's/\([a-zA-Z]\):[\\\/]/\/\1\//g' \
        -e 's/\\/\//g' \
        -e 's/;\//:\//g'
      ):$PATH
      export PATH
      ;;
    esac
  done
  IFS=$OLDIFS
  popd >/dev/null
}

# Make sure all build dependencies are present and platform specific
# environment variables are set.
function check::build::env() {
  init-msenv

  # Required programs that may be missing on Windows
  REQUIRED_PROGS=(
    bash
    sed
    git
    openssl
    find
    grep
    xargs
    pwd
    curl
    rm
    cat
    # strings
  )

  # Check that required programs exist on the system.
  # If they are missing, we abort.
  for f in "${REQUIRED_PROGS[@]}"; do
    if ! has-binary "$f"; then
      echo "Error: '$f' is not installed." >&2
      exit 1
    fi
  done
}

# Compile using ninja.
#
# $1 The output directory, 'out/$TARGET_CPU/Debug', or 'out/$TARGET_CPU/Release'
# $2 Additional gn arguments
function compile::ninja() {
  local outputdir="$1"
  local gn_args="$2"

  echo "Generating project files with: $gn_args"
  gn gen $outputdir --args="$gn_args" -v
  pushd $outputdir >/dev/null
  # ninja -v -C  .
  ninja -C .
  popd >/dev/null
}

# Combine built static libraries into one library.
function combine::static() {
  local outputdir="$1"
  local libname="$2"

  echo $libname
  pushd $outputdir >/dev/null
  rm -f $libname.*

  # Find only the libraries we need
  local whitelist="boringssl.dll.lib|protobuf_lite.dll.lib|webrtc\.lib|field_trial_default.lib|metrics_default.lib"
  cat .ninja_log | tr '\t' '\n' | grep -E $whitelist | sort -u >$libname.list

  # Combine all objects into one static library
  lib.exe /OUT:$libname.lib @$libname.list
  popd >/dev/null
}

# Compile the libraries.
#
function compile() {
  local target_cpu="$1"
  local configs="$2"

  local common_args="rtc_include_tests=true treat_warnings_as_errors=false use_rtti=true is_component_build=false enable_iterator_debugging=false is_clang=false"
  local target_args="target_os=\"win\" target_cpu=\"$target_cpu\""

  pushd webrtc_src/src >/dev/null
  for cfg in $configs; do
    [ "$cfg" = 'Release' ] && common_args+=' is_debug=false strip_debug_info=true symbol_level=0'
    compile::ninja "out/$target_cpu/$cfg" "$common_args $target_args"

    combine::static "out/$target_cpu/$cfg" libwebrtc
  done
  popd >/dev/null
}

# Package a compiled build into an archive file in the output directory.
function package::prepare() {
  local outdir="$1"
  local configs="$2"

  CP='cp'

  # Create directory structure
  mkdir -p $outdir/webrtc/include

  pushd webrtc_src >/dev/null
  pushd src >/dev/null

  local header_source_dir=.

  # Copy header files, skip third_party dir
  #find $header_source_dir -path './third_party' -prune -o -type f \( -name '*.h' \) -print | \
  find $header_source_dir -type f \( -name '*.h' \) -print | \
  xargs -I '{}' $CP --parents '{}' $outdir/webrtc/include

  # Find and copy dependencies
  # The following build dependencies were excluded:
  # gflags, ffmpeg, openh264, openmax_dl, winsdk_samples, yasm
  find $header_source_dir -name '*.h' -o -name README -o -name LICENSE -o -name COPYING | \
  grep './third_party' | \
  grep -E 'boringssl|expat/files|jsoncpp/source/json|libjpeg|libjpeg_turbo|libsrtp|libyuv|libvpx|opus|protobuf|usrsctp/usrsctpout/usrsctpout' | \
  xargs -I '{}' $CP --parents '{}' $outdir/webrtc/include

  popd >/dev/null

  # Find and copy libraries
  for cfg in $configs; do
    mkdir -p $outdir/$TARGET_CPU/$cfg
    pushd $outdir/$TARGET_CPU/$cfg >/dev/null
    mkdir -p $outdir/webrtc/lib/$TARGET_CPU/$cfg
    find . -name '*.so' -o -name '*.dll' -o -name '*.lib' -o -name '*.a' -o -name '*.jar' | \
    grep -E 'libwebrtc' | \
    xargs -I '{}' $CP '{}' $outdir/webrtc/lib/$TARGET_CPU/$cfg
    popd >/dev/null
  done

  popd >/dev/null
}
