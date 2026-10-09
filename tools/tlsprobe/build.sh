#!/usr/bin/env bash
# Build a TLS probe title in the Lima VM (`tools/tlsprobe/build.sh EF|N`), same SDK, compiler wrappers
# and link recipe as the RPCS3 title (tools/launcher/build.sh is the template).
#   EF -> PPSA99391 (emulated TLS; stock E + fast F), out ~/work/tlsprobe-EF
#   N  -> PPSA99392 (native TLS; PT_TLS linker script), out ~/work/tlsprobe-N
set -euo pipefail
variant=${1:?usage: build.sh EF|EF2|N}
src=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
vulkan=~/work/PS5_Vulkan
sdk=~/work/ps5-rpcs3-app/.deps/native/ps5-payload-sdk
out=${2:-$HOME/work/tlsprobe-$variant}
case "$variant" in
  EF)  title_id=PPSA99391 ;;
  EF2) title_id=PPSA99393 ;;
  N)   title_id=PPSA99392 ;;
  *) echo "variant must be EF, EF2 or N" >&2; exit 1 ;;
esac
export PS5_CLANG=${PS5_CLANG:-/usr/bin/clang}
tool="$vulkan/build/host/ps5-native-tool"
native="$vulkan/tooling/native"
mkdir -p "$out/obj" "$out/app/sce_module" "$out/app/sce_sys"
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vulkan/tooling/prospero-clang18" "$@"; }

cc -std=c++20 -O2 -fno-exceptions -fno-rtti -c "$native/app_crt.cpp" -o "$out/obj/app_crt.o"

objs=("$out/obj/app_crt.o")
if [ "$variant" = N ]; then
  cc -std=c++20 -O2 -fno-exceptions -fno-rtti -fno-emulated-tls -ftls-model=initial-exec \
     -DPROBE_N -DPROBE_TAG='"N"' -I"$src" -c "$src/probe.cpp" -o "$out/obj/probe.o"
else
  fdefs=""
  tag=EF
  if [ "$variant" = EF2 ]; then fdefs="-DTLS_F_AS_STOCK"; tag=EF2; fi
  cc -std=c++20 -O2 -fno-exceptions -fno-rtti -DPROBE_EF -DPROBE_TAG="\"$tag\"" -I"$src" -c "$src/probe.cpp" -o "$out/obj/probe.o"
  cc -std=c11 -O2 -Wall $fdefs -c "$src/fast_emutls.c" -o "$out/obj/fast_emutls.o"
  objs+=("$out/obj/fast_emutls.o")
fi
objs+=("$out/obj/probe.o")

"$sdk/bin/llvm-ar" rc "$out/obj/empty.a"
# shellcheck source=/dev/null
source "$vulkan/tools/radv-link.sh"
radv_link_recipe "$vulkan" "$sdk" "$out/obj/empty.a"

linker_script=("${radv_linker_script[@]}")
if [ "$variant" = N ]; then
  # Same unwind script, but its INCLUDE ps5-pie.ld resolves to the PT_TLS copy first.
  linker_script=(-T "$vulkan/tooling/psbc/ps5-pie-unwind.ld" -L "$src/ld")
fi

"$sdk/bin/prospero-lld" --error-limit=0 "${linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$native/app-symbols.map" --exclude-libs=ALL -e _start -o "$out/obj/probe.elf" \
    "${objs[@]}" "${radv_link_inputs[@]}" --as-needed "$sdk"/target/lib/*.so

"$tool" link --in "$out/obj/probe.elf" --out "$out/obj/eboot.elf" --stub-dir "$sdk/target/lib" \
    --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
"$tool" self --sign --in "$out/obj/eboot.elf" --out "$out/app/eboot.bin" --magic 0x1D3D154F
cp "$vulkan/runtime/libc.prx" "$out/app/sce_module/libc.prx"
sed -e "s/__TITLE_ID__/$title_id/g" -e "s/__CONCEPT_ID__/${title_id#PPSA}/g" \
    "$src/param-template.json" > "$out/app/sce_sys/param.json"
ls -l "$out/app/eboot.bin"
