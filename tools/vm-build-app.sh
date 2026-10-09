#!/usr/bin/env bash
# Build the RPCS3 PS5 title (app/) in the Lima VM `ps5build`.
# The VM sees this repository read-only (virtiofs), so the source is synced into
# ~/work/ps5-rpcs3-app and built there; the result is dist/PPSA99303 in that tree,
# copied back to ./dist/PPSA99303 on the Mac (ignored by git).
#   tools/vm-build-app.sh
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
# RPCS3_BUILD_DIR: the RPCS3 build directory whose rpcs3-link-inputs.txt to link (VM path; default
# ~/work/rpcs3-build). APP_DIR: the VM app folder (default ~/work/ps5-rpcs3-app).
# PS5_NATIVE_TLS=1 builds the title with native TLS.
rpcs3_build_dir=${RPCS3_BUILD_DIR:-}
native_tls=${PS5_NATIVE_TLS:-0}
app_dir_vm=${APP_DIR:-$(limactl shell ps5build -- bash -c 'echo $HOME')/work/ps5-rpcs3-app}
limactl shell ps5build -- bash -c "
set -euo pipefail
export PATH=\$HOME/.local/bin:\$PATH LLVM_CONFIG=/usr/lib/llvm-22/bin/llvm-config PS5_CLANG=/usr/bin/clang
export RPCS3_BUILD_DIR='$rpcs3_build_dir' PS5_NATIVE_TLS='$native_tls' APP_DIR_VM='$app_dir_vm'
rsync -a --delete --exclude=/.git --exclude=/build --exclude=/dist --exclude=/.deps --exclude=/klog '$here/app/' \$APP_DIR_VM/
cd \$APP_DIR_VM
touch ps5/CMakeLists.txt  # re-glob sources (files may have been added/removed)
git add -A >/dev/null && git commit -qm sync >/dev/null || true
links=\${RPCS3_BUILD_DIR:-\$HOME/work/rpcs3-build}/rpcs3-link-inputs.txt
nflag=
[[ \${PS5_NATIVE_TLS:-0} == 1 ]] && nflag=-DPS5_NATIVE_TLS=ON
if [[ \${NO_CORE:-} != 1 && -f \$links && -f build/ps5/build.ninja ]]; then
  cmake \$nflag -DRPCS3_LINK_INPUTS=\$links build/ps5 > /dev/null   # RPCS3 core linked in
elif [[ -f build/ps5/build.ninja ]]; then
  cmake \$nflag -DRPCS3_LINK_INPUTS= build/ps5 > /dev/null           # home screen only
fi
ps5/tools/build.sh > ~/work/app-build.log 2>&1 || { tail -40 ~/work/app-build.log; exit 1; }
tail -4 ~/work/app-build.log
# The write helper payload the title starts through the ELF loader (tools/iohelper/main.c)
sdk=\$APP_DIR_VM/.deps/native/ps5-payload-sdk
\$sdk/bin/prospero-clang -Wall -Werror -O2 -o dist/PPSA99303/rpcs3ps5-io.elf '$here/tools/iohelper/main.c'
echo '==> write helper: rpcs3ps5-io.elf'
# The eboot of the games' PS5 home-screen tiles (tools/launcher; kit/tiles.cpp copies it into each tile)
bash '$here/tools/launcher/build.sh' ~/work/tile-launcher > ~/work/tile-launcher.log 2>&1 || { tail -20 ~/work/tile-launcher.log; exit 1; }
cp ~/work/tile-launcher/app/eboot.bin dist/PPSA99303/tile-launcher.bin
\$sdk/bin/prospero-clang -Wall -Werror -O2 -o dist/PPSA99303/rpcs3ps5-launch.elf '$here/tools/launcher/launch.c' -lSceSystemService -lSceUserService
echo '==> tile launcher: tile-launcher.bin'
# RPCS3's per-game settings database (api.rpcs3.net/config, kit/recommend.cpp)
# RPCS3's per-game settings database is not redistributed; fetch it yourself (BUILDING.md). Optional.
if [ -f '$here/app/examples/rpcs3ps5/data/rpcs3-config-db.json' ]; then cp '$here/app/examples/rpcs3ps5/data/rpcs3-config-db.json' dist/PPSA99303/rpcs3-config-db.json; fi
"
mkdir -p "$here/dist"
rm -rf "$here/dist/PPSA99303"
limactl copy -r "ps5build:$app_dir_vm/dist/PPSA99303" "$here/dist/"
du -sh "$here/dist/PPSA99303"
