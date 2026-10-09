#!/usr/bin/env bash
# Apply the KB-RPCS3 patch series to a fresh checkout of upstream RPCS3 at the pinned revision.
#
#   port/apply-rpcs3-patches.sh <target-directory>
#
# The series must be applied with `git am --keep-cr`: several upstream RPCS3 files use CRLF line endings,
# and without --keep-cr git strips the carriage returns from the patches and they no longer apply.
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

UPSTREAM_URL=https://github.com/RPCS3/rpcs3.git
UPSTREAM_REV=46aee28f8   # the RPCS3 revision the series was developed and verified against

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
target=${1:?usage: apply-rpcs3-patches.sh <target-directory>}

if [[ -e $target ]]; then
    echo "error: $target already exists" >&2
    exit 1
fi

git clone "$UPSTREAM_URL" "$target"
cd "$target"
git checkout -q "$UPSTREAM_REV"
git checkout -q -b kb-rpcs3-0.1.0
git submodule update --init --recursive
git -c user.name="kb-rpcs3 patch apply" -c user.email="apply@localhost" am --keep-cr "$here"/rpcs3-patches/*.patch
echo "Applied $(git rev-list --count "$UPSTREAM_REV"..HEAD) patches on top of RPCS3 $UPSTREAM_REV in $target"
