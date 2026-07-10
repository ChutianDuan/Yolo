#!/usr/bin/env bash
set -u

VCPKG_ROOT="${VCPKG_ROOT:-/root/vcpkg}"
VCPKG_TARGET_TRIPLET="${VCPKG_TARGET_TRIPLET:-x64-linux-gcc15}"
VCPKG_HOST_TRIPLET="${VCPKG_HOST_TRIPLET:-x64-linux-gcc15}"
VCPKG_OVERLAY_TRIPLETS="${VCPKG_OVERLAY_TRIPLETS:-$VCPKG_ROOT/custom-triplets}"
VCPKG_OVERLAY_PORTS="${VCPKG_OVERLAY_PORTS:-$VCPKG_ROOT/custom-ports}"

TOOLS_ROOT="$VCPKG_ROOT/downloads/tools"
export PATH="$TOOLS_ROOT/gperf-3.1/bin:$TOOLS_ROOT/flex-2.6.4/bin:$TOOLS_ROOT/bison-3.8.2/bin:$TOOLS_ROOT/autotools/bin:$TOOLS_ROOT/nasm-3.01/bin:$VCPKG_ROOT/.toolchains/gcc15/bin:$VCPKG_ROOT:$PATH"
export ACLOCAL_PATH="$VCPKG_ROOT/installed/$VCPKG_TARGET_TRIPLET/share/aclocal:$TOOLS_ROOT/autotools/share/aclocal${ACLOCAL_PATH:+:$ACLOCAL_PATH}"

missing=0

require_cmd() {
  if command -v "$1" >/dev/null 2>&1; then
    printf '[ok] command: %s\n' "$1"
    return
  fi

  printf '[missing] command: %s\n' "$1"
  missing=1
}

require_path() {
  if [ -e "$1" ]; then
    printf '[ok] path: %s\n' "$1"
    return
  fi

  printf '[missing] path: %s\n' "$1"
  missing=1
}

has_autoconf_archive() {
  aclocal_dirs="/usr/share/aclocal:/usr/local/share/aclocal"
  if [ -n "${ACLOCAL_PATH:-}" ]; then
    aclocal_dirs="$ACLOCAL_PATH:$aclocal_dirs"
  fi

  old_ifs="$IFS"
  IFS=':'
  for dir in $aclocal_dirs; do
    if [ -n "$dir" ] && find "$dir" -maxdepth 1 -name 'ax_*.m4' -print -quit 2>/dev/null | grep -q .; then
      IFS="$old_ifs"
      return 0
    fi
  done
  IFS="$old_ifs"
  return 1
}

printf 'VCPKG_ROOT=%s\n' "$VCPKG_ROOT"
printf 'VCPKG_TARGET_TRIPLET=%s\n' "$VCPKG_TARGET_TRIPLET"
printf 'VCPKG_HOST_TRIPLET=%s\n' "$VCPKG_HOST_TRIPLET"
printf 'VCPKG_OVERLAY_TRIPLETS=%s\n\n' "$VCPKG_OVERLAY_TRIPLETS"
printf 'VCPKG_OVERLAY_PORTS=%s\n\n' "$VCPKG_OVERLAY_PORTS"

require_cmd cmake
require_cmd ninja
require_cmd autoconf
require_cmd automake
require_cmd libtoolize
require_cmd gperf
require_cmd flex
require_cmd bison
require_cmd nasm

if has_autoconf_archive; then
  printf '[ok] autoconf-archive macros\n'
else
  printf '[missing] autoconf-archive macros\n'
  missing=1
fi

require_path "$VCPKG_ROOT/vcpkg"
require_path "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
require_path "$VCPKG_OVERLAY_TRIPLETS/$VCPKG_TARGET_TRIPLET.cmake"
require_path "$VCPKG_OVERLAY_PORTS/xcb-cursor/vcpkg.json"
require_path "$VCPKG_ROOT/.toolchains/gcc15/bin/x86_64-conda-linux-gnu-gcc"
require_path "$VCPKG_ROOT/.toolchains/gcc15/bin/x86_64-conda-linux-gnu-g++"

qt_config="$VCPKG_ROOT/installed/$VCPKG_TARGET_TRIPLET/share/Qt6/Qt6Config.cmake"
if [ -e "$qt_config" ]; then
  printf '[ok] Qt6: %s\n' "$qt_config"
else
  printf '[missing] Qt6 for triplet %s\n' "$VCPKG_TARGET_TRIPLET"
  missing=1
fi

require_path "$VCPKG_ROOT/installed/$VCPKG_TARGET_TRIPLET/share/opencv4/OpenCVConfig.cmake"
require_path "$VCPKG_ROOT/installed/$VCPKG_TARGET_TRIPLET/share/onnxruntime/onnxruntimeConfig.cmake"
require_path "$VCPKG_ROOT/installed/$VCPKG_TARGET_TRIPLET/share/jsoncpp/jsoncppConfig.cmake"
require_path "$VCPKG_ROOT/installed/$VCPKG_TARGET_TRIPLET/share/drogon/DrogonConfig.cmake"
require_path "$VCPKG_ROOT/installed/$VCPKG_TARGET_TRIPLET/tools/ffmpeg/ffmpeg"
require_path "$VCPKG_ROOT/installed/$VCPKG_TARGET_TRIPLET/tools/ffmpeg/ffprobe"
require_path "$VCPKG_ROOT/installed/$VCPKG_TARGET_TRIPLET/lib/pkgconfig/xcb-cursor.pc"

if [ "$missing" -eq 0 ]; then
  printf '\nEnvironment check passed.\n'
  exit 0
fi

cat <<EOF

Environment check failed.

Install Qt and native dependencies with the local vcpkg tool cache:
  export PATH=$TOOLS_ROOT/gperf-3.1/bin:$TOOLS_ROOT/flex-2.6.4/bin:$TOOLS_ROOT/bison-3.8.2/bin:$TOOLS_ROOT/autotools/bin:$TOOLS_ROOT/nasm-3.01/bin:$VCPKG_ROOT/.toolchains/gcc15/bin:$VCPKG_ROOT:\${PATH}
  export ACLOCAL_PATH=$VCPKG_ROOT/installed/$VCPKG_TARGET_TRIPLET/share/aclocal:$TOOLS_ROOT/autotools/share/aclocal
  export LD_LIBRARY_PATH=$TOOLS_ROOT/autotools/lib:$VCPKG_ROOT/.toolchains/gcc15/lib:\${LD_LIBRARY_PATH:-}
  $VCPKG_ROOT/vcpkg install 'qtbase[core,widgets,xcb,xrender,fontconfig,png,jpeg]' \\
    'opencv4[core,ffmpeg,jpeg,png,tiff,webp]' onnxruntime drogon jsoncpp 'ffmpeg[ffmpeg,ffprobe,x264]' \\
    --classic \\
    --triplet $VCPKG_TARGET_TRIPLET \\
    --host-triplet $VCPKG_HOST_TRIPLET \\
    --overlay-triplets=$VCPKG_OVERLAY_TRIPLETS \\
    --overlay-ports=$VCPKG_OVERLAY_PORTS \\
    --recurse

After dependencies are present:
  cd qt_ui
  cmake --preset vcpkg-gcc15-release
  cmake --build --preset vcpkg-gcc15-release
EOF

exit 1
