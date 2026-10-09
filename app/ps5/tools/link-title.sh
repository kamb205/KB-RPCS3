#!/usr/bin/env bash
# PS5 Vulkan Template - link the title and package its folder.
#
#   link-title.sh BUILD_DIR PS5_VULKAN_DIR SDK OBJECT...
#
# Run by the build (ps5/CMakeLists.txt) once the objects are compiled. The link
# is PS5_Vulkan's: its RADV release archive (RADV, ACO, NIR and Mesa's runtime,
# built from PS5_Mesa), its link recipe (tools/radv-link.sh: the platform layer
# of this title's SDK pin, the heap and thread wraps, the libc names bound to ps5_*),
# its CRT, its native tool (ELF -> the console's fake SELF) and its libc.prx,
# as its CTS title links them. The title folder, dist/<TITLE_ID>/, gets the
# signed eboot.bin, sce_sys/, the shaders of the samples linked in and the
# assets (tools/build-assets.py).
#
# Copyright (C) 2026 Mihawk
# SPDX-License-Identifier: MIT

set -euo pipefail
[[ -n ${LINK_TRACE:-} ]] && set -x

work=$1 vulkan=$2 sdk=$3
shift 3
objects=("$@")
ps5=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
root=$(dirname "$ps5")
archive=${RADV_ARCHIVE:-$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a}
export PS5_CLANG=${PS5_CLANG:-$(command -v clang || true)}
tool="$vulkan/build/host/ps5-native-tool"
native="$vulkan/tooling/native"
param="$ps5/sce_sys/param.json"

for file in "$archive" "$tool" "$param" "$sdk/bin/prospero-lld" "$vulkan/tools/radv-link.sh" \
        "$vulkan/runtime/libc.prx" "$work/samples.txt"; do
    [[ -e $file ]] || { echo "missing: $file" >&2; exit 2; }
done
mkdir -p "$work/link/obj" "$work/link/stubs"
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vulkan/tooling/prospero-clang18" "$@"; }

# The CRT. The C++ runtime is libc++abi's (the samples throw and catch), as in
# PS5_Vulkan's CTS title.
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -c "$native/app_crt.cpp" -o "$work/link/obj/app_crt.o"
# RADV calls AGC, which the SDK has no stubs for: these name its imports.
stub() {
    local library=$1 source=$2
    cc -std=c11 -O2 -fPIC -c "$vulkan/$source" -o "$work/link/obj/${library}_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "${library}.prx" \
        -o "$work/link/stubs/${library}.so" "$work/link/obj/${library}_stub.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c

# shellcheck source=/dev/null
source "$vulkan/tools/radv-link.sh"
radv_link_recipe "$vulkan" "$sdk" "$archive" || exit 2
# localeconv in the C locale, '.' the decimal point (the console's gives none, and
# tinygltf's JSON parser then read 0.62 as 0): the platform layer's since SDK fork
# fa69d00 (tools/setup-sdk.sh's pin is never older), bound by PS5_Vulkan's recipe
# since its 6a6dfa6. With an older recipe the title binds it here, kept local as
# the recipe keeps its bound names. The symbol listing is read whole before it
# is searched: piped into grep -q under pipefail, llvm-nm's SIGPIPE made the
# found symbol a miss about half the time once the archive grew (SDK adc8dd7).
platform_has_localeconv=false
grep -q " T ps5_localeconv$" <<<"$("$sdk/bin/llvm-nm" --defined-only "$sdk/target/lib/libps5platform.a" 2>/dev/null)" &&
    platform_has_localeconv=true
if $platform_has_localeconv && [[ " ${radv_link_flags[*]} " != *" --defsym=localeconv=ps5_localeconv "* ]]; then
    printf '{\n    local:\n        localeconv;\n};\n' > "$work/link/localeconv-local.map"
    radv_link_flags+=(--defsym=localeconv=ps5_localeconv --version-script "$work/link/localeconv-local.map")
fi
# RPCS3 PS5: the environment lives in the title (src/ps5_env.c): the console leaves a
# title's getenv import pointing at nothing, and RPCS3 reads it from static constructors.
# Bound like the recipe's names, and kept local for the same reason (no exports).
# clock_gettime / sched_yield / pthread_yield: src/ps5_fastclock.c (no system call: ~1 us and ~28 us each on the
# console, and RPCS3's wait loops call them constantly)
rpcs3_local=(getenv setenv unsetenv putenv cpuset_getaffinity pathconf dlopen dlsym dlerror dlclose
    clock_gettime sched_yield pthread_yield)
for name in "${rpcs3_local[@]}"; do
    radv_link_flags+=("--defsym=$name=rpcs3ps5_$name")
done
# ... and the platform layer's drop-ins (ps5platform/libc.h) for the libc functions RPCS3
# imports that a title cannot use as the console gives them: getcwd faults, statfs and
# sysconf answer wrongly for a title, pthread_exit must run thread_local destructors first,
# the process functions must fail cleanly.
# Not pthread_exit or sysconf: ps5_pthread_exit and ps5_sysconf call the system's own, and
# binding the name to them made each call itself until the stack ran out (console run 4).
# Any name whose ps5_ function references it is refused here for the same reason.
platform_objects=$(mktemp -d)
(cd "$platform_objects" && "$sdk/bin/llvm-ar" x "$sdk/target/lib/libps5platform.a")
for name in fchown fork fstatfs gai_strerror getcwd getnameinfo isatty link mkstemp \
        pthread_getaffinity_np pthread_setaffinity_np readlink realpath setsid statfs symlink \
        umask wait4; do
    for object in "$platform_objects"/*.o; do
        if grep -q " T ps5_$name$" <<<"$("$sdk/bin/llvm-nm" --defined-only "$object" 2>/dev/null)"; then
            if grep -qw "$name" <<<"$("$sdk/bin/llvm-nm" -u "$object" 2>/dev/null)"; then
                echo "link-title.sh: ps5_$name calls $name itself; it cannot be bound in its place" >&2
                exit 1
            fi
        fi
    done
    radv_link_flags+=("--defsym=$name=ps5_$name")
    rpcs3_local+=("$name")
done
rm -rf "$platform_objects"
{ printf '{\n    local:\n'; printf '        %s;\n' "${rpcs3_local[@]}"; printf '};\n'; } > "$work/link/rpcs3-local.map"
radv_link_flags+=(--version-script "$work/link/rpcs3-local.map")

# RPCS3 PS5: the emulator's static archives (its frontend, rpcs3_emu, LLVM, FFmpeg and the
# rest), one path per line in the file RPCS3_LINK_INPUTS names; linked as one group
rpcs3_inputs=()
if [[ -n ${RPCS3_LINK_INPUTS:-} && -f $RPCS3_LINK_INPUTS ]]; then
    mapfile -t rpcs3_archives < "$RPCS3_LINK_INPUTS"
    rpcs3_inputs=(--start-group "${rpcs3_archives[@]}" --end-group)
fi
"$sdk/bin/prospero-lld" --error-limit=0 "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$native/app-symbols.map" --exclude-libs=ALL \
    -e _start -o "$work/link/llvm-pie.elf" \
    "$work/link/obj/app_crt.o" "${objects[@]}" "${rpcs3_inputs[@]}" \
    "$work/link/stubs/libSceAgc.so" "$work/link/stubs/libSceAgcDriver.so" \
    "${radv_link_inputs[@]}" \
    --as-needed "$sdk"/target/lib/*.so
# A title whose localeconv is still the console's reads every glTF fraction as 0
# (black materials, models scaled to nothing): refuse it here, not on the console
if $platform_has_localeconv &&
        grep -q " U localeconv$" <<<"$("$sdk/bin/llvm-nm" "$work/link/llvm-pie.elf" 2>/dev/null)"; then
    echo "link-title.sh: localeconv is not bound to the platform layer's ps5_localeconv" >&2
    exit 1
fi
"$tool" link --in "$work/link/llvm-pie.elf" --out "$work/eboot.elf.new" \
    --stub-dir "$sdk/target/lib" --stub "$work/link/stubs/libSceAgc.so" \
    --stub "$work/link/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf

# The title folder
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$param")
app="$root/dist/$title_id"
mkdir -p "$app/sce_sys" "$app/sce_module" "$app/shaders/glsl"
"$tool" self --sign --in "$work/eboot.elf.new" --out "$app/eboot.bin" --magic 0x1D3D154F
"$tool" self --inspect --file "$app/eboot.bin" > /dev/null
cp "$param" "$app/sce_sys/param.json"
for asset in icon0.png pic0.dds pic1.dds snd0.at9; do
    [[ -f $ps5/sce_sys/$asset ]] && cp "$ps5/sce_sys/$asset" "$app/sce_sys/$asset"
done
(cd "$vulkan/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)
cp "$vulkan/runtime/libc.prx" "$app/sce_module/libc.prx"
# The precompiled SPIR-V of the base class and of each sample linked in
# (CMake writes the list without a final newline: read then reports end of file).
# A sample that draws only with the UI kit has none: the kit carries its own.
IFS=';' read -r -a samples < "$work/samples.txt" || (( ${#samples[@]} ))
rm -rf "$app/shaders/glsl"
mkdir -p "$app/shaders/glsl"
for dir in base "${samples[@]}"; do
    compgen -G "$root/shaders/glsl/$dir/*.spv" > /dev/null || continue
    mkdir -p "$app/shaders/glsl/$dir"
    cp "$root/shaders/glsl/$dir/"*.spv "$app/shaders/glsl/$dir/"
done
mv "$work/eboot.elf.new" "$work/eboot.elf"
# RPCS3 PS5: the title's code is execute-only on the console, and RPCS3's access violation handler
# reads the instruction that faulted; it reads it from this copy of the executable segment instead
# (rpcs3/ps5/ps5_text_mirror.cpp in the RPCS3 tree): "RPCS3PS5TEXT", link address, size, bytes.
if [[ -n ${RPCS3_LINK_INPUTS:-} ]]; then
    python3 - "$work/eboot.elf" "$app/rpcs3ps5-text.bin" <<'PY'
import struct, sys
elf = open(sys.argv[1], "rb").read()
assert elf[:4] == b"\x7fELF"
phoff, = struct.unpack_from("<Q", elf, 0x20)
phentsize, phnum = struct.unpack_from("<HH", elf, 0x36)
for i in range(phnum):
    p_type, p_flags, p_offset, p_vaddr, _, p_filesz, _, _ = struct.unpack_from("<IIQQQQQQ", elf, phoff + i * phentsize)
    if p_type == 1 and p_flags & 1:  # the first executable PT_LOAD
        with open(sys.argv[2], "wb") as out:
            out.write(b"RPCS3PS5TEXT" + struct.pack("<QQ", p_vaddr, p_filesz))
            out.write(elf[p_offset:p_offset + p_filesz])
        print("==> code copy: vaddr %#x, %d bytes" % (p_vaddr, p_filesz))
        break
else:
    sys.exit("link-title.sh: no executable segment in eboot.elf")
PY
fi
archive_revision=$(sed -n 's/^revision: //p' "$(dirname "$(dirname "$archive")")/PROVENANCE.txt" 2>/dev/null || true)
printf '==> %s: %s (eboot.bin %s bytes; %d samples; RADV %s)\n' "$title_id" "$app" \
    "$(stat -c %s "$app/eboot.bin")" "${#samples[@]}" "${archive_revision:0:11}"
# The notices beside eboot.bin: LEGAL.txt and licenses/, each part with its revision
python3 "$ps5/tools/stage-notices.py" "$app" "$vulkan" "$archive"
