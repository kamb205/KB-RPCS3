#!/usr/bin/env bash
# Build the game tile launcher eboot in the Lima VM (run inside it): out/eboot.bin + sce_module/libc.prx
# Same recipe as app/ps5/tools/link-title.sh, without RADV.
set -euo pipefail
src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
vulkan=~/work/PS5_Vulkan
sdk=~/work/ps5-rpcs3-app/.deps/native/ps5-payload-sdk
out=${1:-~/work/tile-launcher}
export PS5_CLANG=${PS5_CLANG:-/usr/bin/clang}
tool="$vulkan/build/host/ps5-native-tool"
native="$vulkan/tooling/native"
mkdir -p "$out/obj" "$out/app/sce_module"
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vulkan/tooling/prospero-clang18" "$@"; }
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -c "$native/app_crt.cpp" -o "$out/obj/app_crt.o"
cc -std=c11 -O2 -Wall -Werror -c "$src/main.c" -o "$out/obj/main.o"
"$sdk/bin/llvm-ar" rc "$out/obj/empty.a"
# shellcheck source=/dev/null
source "$vulkan/tools/radv-link.sh"
radv_link_recipe "$vulkan" "$sdk" "$out/obj/empty.a"
"$sdk/bin/prospero-lld" --error-limit=0 "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$native/app-symbols.map" --exclude-libs=ALL -e _start -o "$out/obj/launcher.elf" \
    "$out/obj/app_crt.o" "$out/obj/main.o" "${radv_link_inputs[@]}" --as-needed "$sdk"/target/lib/*.so
"$tool" link --in "$out/obj/launcher.elf" --out "$out/obj/eboot.elf" --stub-dir "$sdk/target/lib" \
    --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
"$tool" self --sign --in "$out/obj/eboot.elf" --out "$out/app/eboot.bin" --magic 0x1D3D154F
cp "$vulkan/runtime/libc.prx" "$out/app/sce_module/libc.prx"
ls -l "$out/app/eboot.bin"
