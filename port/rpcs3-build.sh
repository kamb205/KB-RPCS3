#!/usr/bin/env bash
# Configure + build RPCS3's emulator library (rpcs3_emu, no Qt) for the PS5, in the VM.
# Runs inside `ps5build` (copy it in, or run via: limactl shell ps5build -- bash <this file>).
#   rpcs3-build.sh [ninja target...]     default target: rpcs3_emu
# Env (defaults reproduce the original build):
#   SRC_DIR         RPCS3 source tree (default $HOME/work/rpcs3)
#   BUILD_DIR       build directory (default $HOME/work/rpcs3-build); rpcs3-link-inputs.txt lands here too
#   EXTRA_CFLAGS    appended to CMAKE_C_FLAGS and CMAKE_CXX_FLAGS (e.g. "-fno-emulated-tls")
#   CONFIGURE_ONLY  1 = configure and stop (inspect the flags before a long build)
set -euo pipefail
export PATH=$HOME/.local/bin:$PATH LLVM_CONFIG=/usr/lib/llvm-22/bin/llvm-config PS5_CLANG=/usr/bin/clang
W=$HOME/work
SRC=${SRC_DIR:-$W/rpcs3}
B=${BUILD_DIR:-$W/rpcs3-build}
EXTRA_CFLAGS=${EXTRA_CFLAGS:-}
SDK=$W/ps5-rpcs3-app/.deps/native/ps5-payload-sdk
NAT=$W/PS5_RetroArch/.deps/native
FF=$NAT/ffmpeg-ps5/lib
if [[ ! -f $B/build.ninja ]]; then
  mkdir -p "$B"
  cmake -S "$SRC" -B "$B" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$SDK/toolchain/prospero.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=OFF -DCMAKE_CXX_SCAN_FOR_MODULES=OFF \
    -DCMAKE_C_FLAGS="-march=znver2 -DZSTD_TRACE=0 $EXTRA_CFLAGS" -DCMAKE_CXX_FLAGS="-march=znver2 -DZSTD_TRACE=0 $EXTRA_CFLAGS" \
    -DUSE_NATIVE_INSTRUCTIONS=OFF -DUSE_LTO=OFF -DUSE_PRECOMPILED_HEADERS=OFF \
    -DWITH_LLVM=ON -DBUILD_LLVM=OFF -DSTATIC_LINK_LLVM=ON -DLLVM_DIR="$NAT/llvm-ps5/lib/cmake/llvm" \
    -DUSE_FAUDIO=OFF -DUSE_SDL=OFF -DUSE_LIBEVDEV=OFF -DUSE_DISCORD_RPC=OFF -DUSE_GAMEMODE=OFF \
    -DUSE_SYSTEM_ZLIB=ON -DZLIB_INCLUDE_DIR="$NAT/zlib/root/usr/include" -DZLIB_LIBRARY="$NAT/zlib/root/usr/lib/libz.a" \
    -DUSE_VULKAN=ON -DPS5_VULKAN_HEADERS_DIR="$W/deps/Vulkan-Headers-1.4.354" -DPS5_VOLK_DIR="$W/deps/volk-vulkan-sdk-1.4.350.1" \
    -DUSE_SYSTEM_CURL=OFF -DUSE_SYSTEM_OPENCV=OFF -DUSE_SYSTEM_OPENAL=OFF -DUSE_SYSTEM_SDL=OFF \
    -DUSE_SYSTEM_FFMPEG=ON -DFFMPEG_INCLUDE_DIR="$NAT/ffmpeg-ps5/include" \
    -DFFMPEG_LIBRARIES="$FF/libavformat.a;$FF/libavcodec.a;$FF/libswscale.a;$FF/libswresample.a;$FF/libavutil.a" \
    -DIconv_INCLUDE_DIR="$NAT/libiconv-ps5/include" -DIconv_LIBRARY="$NAT/libiconv-ps5/lib/libiconv.a" -DIconv_IS_BUILT_IN=OFF \
    > "$B.configure.log" 2>&1 || { tail -60 "$B.configure.log"; exit 1; }
fi
if [[ ${CONFIGURE_ONLY:-} == 1 ]]; then echo "CONFIGURED $B"; exit 0; fi
ninja -C "$B" -k 0 -j "${JOBS:-6}" "${@:-rpcs3_emu}" > "$B.log" 2>&1 && echo BUILD OK || { echo "BUILD FAILED: $(grep -c 'error:' "$B.log") errors (log $B.log)"; exit 1; }

# The archives the PS5 title links: read from the never-linked probe's link command,
# in its order (absolute paths; the SDK's own libs and -l flags are the title's business)
if [[ " ${*:-rpcs3_emu} " == *" rpcs3_ps5 "* ]]; then
  cmd=$(ninja -C "$B" -t commands rpcs3_ps5_linkline | tail -1)
  : > "$B/rpcs3-link-inputs.txt"
  for word in $cmd; do
    case $word in
      */libz.a) ;;  # RADV's archive carries zlib already
      *.a)
        [[ $word == /* ]] || word="$B/$word"
        [[ -f $word ]] && echo "$word" >> "$B/rpcs3-link-inputs.txt" ;;
    esac
  done
  echo "link inputs: $(wc -l < "$B/rpcs3-link-inputs.txt") archives -> $B/rpcs3-link-inputs.txt"
fi
