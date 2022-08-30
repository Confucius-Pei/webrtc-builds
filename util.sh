# Cleanup the output directory.
#
# $1: The output directory.
function clean() {
  local outdir="$1"
  rm -rf $outdir/* $outdir/.gclient*
}

# Make sure a package is installed. Depends on sudo to be installed first.
#
# $1: The name of the package
# $2: Existence check binary. Defaults to name of the package.
function ensure-package() {
  local name="$1"
  local binary="${2:-$1}"
  if ! which $binary > /dev/null ; then
    sudo apt-get update -qq
    sudo apt-get install -y $name
  fi
}

# Check if any of the arguments is executable (logical OR condition).
# Using plain "type" without any option because has-binary is intended
# to know if there is a program that one can call regardless if it is
# an alias, builtin, function, or a disk file that would be executed.
function has-binary () {
  type "$1" &> /dev/null ;
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
      INCLUDE=*|LIB=*|LIBPATH=*)
        export $line ;;
      PATH=*)
        PATH=$(echo $line | sed \
          -e 's/PATH=//' \
          -e 's/\([a-zA-Z]\):[\\\/]/\/\1\//g' \
          -e 's/\\/\//g' \
          -e 's/;\//:\//g'):$PATH
        export PATH
        ;;
      esac
    done
    IFS=$OLDIFS
  popd >/dev/null
}

# Make sure all build dependencies are present and platform specific
# environment variables are set.
#
# $1: The platform type.
function check::build::env() {
    init-msenv

    # Required programs that may be missing on Windows
    # TODO: check before running platform specific commands
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
    for f in "${REQUIRED_PROGS[@]}" ; do
      if ! has-binary "$f" ; then
        echo "Error: '$f' is not installed." >&2
        exit 1
      fi
}

# Compile using ninja.
#
# $1 The output directory, 'out/$TARGET_CPU/Debug', or 'out/$TARGET_CPU/Release'
# $2 Additional gn arguments
function compile::ninja() {
  local outputdir="$1"
  local gn_args="$2"

  echo "Generating project files with: $gn_args"
  gn gen $outputdir --args="$gn_args"
  pushd $outputdir >/dev/null
    # ninja -v -C  .
    ninja -C  .
  popd >/dev/null
}

# Combine built static libraries into one library.
#
# NOTE: This method is currently preferred since combining .o objects is
# causing undefined references to libvpx intrinsics on both Linux and Windows.
#
# The Microsoft Windows tools use different file extensions than the other tools:
# '.obj' as the object file extension, instead of '.o'
# '.lib' as the static library file extension, instead of '.a'
# '.dll' as the shared library file extension, instead of '.so'
#
# The Microsoft Windows tools have different names than the other tools:
# 'lib' as the librarian, instead of 'ar'.
#
# $1: The platform
# $2: The list of object file paths to be combined
# $3: The output library name
function combine::static() {
  local platform="$1"
  local outputdir="$2"
  local libname="$3"

  echo $libname
  pushd $outputdir >/dev/null
    rm -f $libname.*

    # Find only the libraries we need
    if [ $platform = 'win' ]; then
      local whitelist="boringssl.dll.lib|protobuf_lite.dll.lib|webrtc\.lib|field_trial_default.lib|metrics_default.lib"
    else
      local whitelist="boringssl\.a|protobuf_full\.a|webrtc\.a|field_trial_default\.a|metrics_default\.a"
    fi
    cat .ninja_log | tr '\t' '\n' | grep -E $whitelist | sort -u >$libname.list

    # Combine all objects into one static library
    case $platform in
    win)
      lib.exe /OUT:$libname.lib @$libname.list
      ;;
    *)
      # Combine *.a static libraries
      echo "CREATE $libname.a" >$libname.ar
      while read a; do
        echo "ADDLIB $a" >>$libname.ar
      done <$libname.list
      echo "SAVE" >>$libname.ar
      echo "END" >>$libname.ar
      ar -M < $libname.ar
      ranlib $libname.a
      ;;
    esac
  popd >/dev/null
}

# Compile the libraries.
#
# $1: The platform type.
# $2: The output directory.
# $6: The src directory.
function compile() {
  local platform="$1"
  local outdir="$2"
  local target_cpu="$3"
  local configs="$4"
  local srcdir="$5"

  # Set default default common  and target args.
  # `rtc_include_tests=false`: Disable all unit tests
  # `treat_warnings_as_errors=false`: Don't error out on compiler warnings
  local common_args="rtc_include_tests=false treat_warnings_as_errors=false"
  local target_args="target_os=win target_cpu=\"$target_cpu\""

  # Build WebRTC with RTII enbled.
  [ $ENABLE_RTTI = 1 ] && common_args+=" use_rtti=true"

  # Static vs Dynamic CRT: When `is_component_build` is false static CTR will be
  # enforced.By default Debug builds are dynamic and Release builds are static.
  common_args+=" is_component_build=false"

  # `enable_iterator_debugging=false`: Disable libstdc++ debugging facilities
  # unless all your compiled applications and dependencies define _GLIBCXX_DEBUG=1.
  # This will cause errors like: undefined reference to `non-virtual thunk to
  # cricket::VideoCapturer::AddOrUpdateSink(rtc::VideoSinkInterface<webrtc::VideoFrame>*,
  # rtc::VideoSinkWants const&)'
  common_args+=" enable_iterator_debugging=false"

  # Use clang or gcc to compile WebRTC.
  # The default compiler used by Chromium/WebRTC is clang, so there are frequent
  # bugs and incompatabilities with gcc, especially with newer versions >= 4.8.
  # Use gcc at your own risk, but it may be necessary if your compiler doesn't
  # like the clang compiled libraries, so the option is there.
  # Set `is_clang=false` and `use_sysroot=false` to build using gcc.
  common_args+=" is_clang=false"
  [ $platform = 'linux' ] && common_args+=" use_sysroot=false linux_use_bundled_binutils=false use_custom_libcxx=false use_custom_libcxx_for_host=false"

  pushd $srcdir/src >/dev/null
    for cfg in $configs; do
      [ "$cfg" = 'Release' ] && common_args+=' is_debug=false strip_debug_info=true symbol_level=0'
      compile::ninja "$outdir/$target_cpu/$cfg" "$common_args $target_args"

      # Method 1: Merge the static .a/.lib libraries.
      combine::static $platform "$outdir/$target_cpu/$cfg" libwebrtc
    done
  popd >/dev/null
}

# Package a compiled build into an archive file in the output directory.
#
# $1: The platform type.
# $2: The output directory.
# $3: The package filename.
# $4: The project's resource dirctory.
# $5: The build configurations.
# $6: The revision number.
# $7: The src directory.
function package::prepare() {
  local platform="$1"
  local outdir="$2"
  local package_filename="$3"
  local resource_dir="$4"
  local configs="$5"
  local srcdir="$6"
  
  if [ $platform = 'mac' ]; then
    CP='gcp'
  else
    CP='cp'
  fi

   # Create directory structure
  mkdir -p $outdir/$package_filename/include

  pushd $srcdir >/dev/null
    pushd src >/dev/null

      local header_source_dir=.

      # Copy header files, skip third_party dir
      #find $header_source_dir -path './third_party' -prune -o -type f \( -name '*.h' \) -print | \
      find $header_source_dir -type f \( -name '*.h' \) -print | \
        xargs -I '{}' $CP --parents '{}' $outdir/$package_filename/include
        
      # Find and copy dependencies
      # The following build dependencies were excluded: 
      # gflags, ffmpeg, openh264, openmax_dl, winsdk_samples, yasm
      find $header_source_dir -name '*.h' -o -name README -o -name LICENSE -o -name COPYING | \
        grep './third_party' | \
        grep -E 'boringssl|expat/files|jsoncpp/source/json|libjpeg|libjpeg_turbo|libsrtp|libyuv|libvpx|opus|protobuf|usrsctp/usrsctpout/usrsctpout' | \
        xargs -I '{}' $CP --parents '{}' $outdir/$package_filename/include

    popd >/dev/null

    # Find and copy libraries
    for cfg in $configs; do
      mkdir -p $outdir/$TARGET_CPU/$cfg
      pushd $outdir/$TARGET_CPU/$cfg >/dev/null
        mkdir -p $outdir/$package_filename/lib/$TARGET_CPU/$cfg
        find . -name '*.so' -o -name '*.dll' -o -name '*.lib' -o -name '*.a' -o -name '*.jar' | \
          grep -E 'libwebrtc' | \
          xargs -I '{}' $CP '{}' $outdir/$package_filename/lib/$TARGET_CPU/$cfg
      popd >/dev/null
    done

    # Create pkgconfig files on linux
    if [ $platform = 'linux' ]; then
      for cfg in $configs; do
        mkdir -p $outdir/$package_filename/lib/$TARGET_CPU/$cfg/pkgconfig
        CONFIG=$cfg envsubst '$CONFIG' < $resource_dir/pkgconfig/libwebrtc_full.pc.in > \
          $package_filename/lib/$TARGET_CPU/$cfg/pkgconfig/libwebrtc_full.pc
      done
    fi

  popd >/dev/null
}

# This interprets a pattern and returns the interpreted one.
function interpret-pattern() {
  local pattern="$1"
  local platform="$2"
  local target_cpu="$3"

  pattern=${pattern//%p%/$platform}
  pattern=${pattern//%tc%/$target_cpu}

  echo "$pattern"
}
