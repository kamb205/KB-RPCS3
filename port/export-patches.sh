#!/usr/bin/env bash
# Export the VM's RPCS3 `ps5-combo` branch (the combo worktree, on top of upstream 46aee28f8) into port/rpcs3-patches/.
# Re-apply on a fresh clone: git checkout -b ps5 46aee28f8 && git am port/rpcs3-patches/*.patch
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
limactl shell ps5build -- bash -c 'cd ~/work/rpcs3 && rm -rf /tmp/rpcs3-patches && git format-patch -q -o /tmp/rpcs3-patches 46aee28f8..ps5-combo && tar -C /tmp -cf - rpcs3-patches' \
  | (rm -rf "$here/rpcs3-patches" && tar -C "$here" -xf -)
ls "$here/rpcs3-patches"
