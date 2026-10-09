#!/usr/bin/env bash
# Install dist/PPSA99303 on the PS5 as a ShadowMountPlus folder title, optionally
# asking the next launch to run the feasibility probe.
#   tools/deploy-title.sh            install/update the title
#   tools/deploy-title.sh --probe    ... and request the probe (word "all") for the next launch
#   tools/deploy-title.sh --results  fetch probe-results.txt into ./results/
#   tools/deploy-title.sh --logs     fetch rpcs3/ps5-title.log and rpcs3/RPCS3.log into ./results/
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ps5=${PS5_IP:-YOUR_PS5_IP}
remote=/data/homebrew/PPSA99303
ftp="ftp://$ps5:1337"
case ${1:-} in
--logs)
    mkdir -p "$here/results"
    stamp=$(date +%Y%m%d-%H%M%S)
    for name in ps5-title.log RPCS3.log; do
        curl -sS "$ftp$remote/rpcs3/$name" -o "$here/results/$stamp-$name" && echo "saved: results/$stamp-$name"
    done
    tail -40 "$here/results/$stamp-ps5-title.log" 2>/dev/null; exit 0 ;;
--results)
    mkdir -p "$here/results"
    out="$here/results/probe-$(date +%Y%m%d-%H%M%S).txt"
    curl -sS "$ftp$remote/probe-results.txt" -o "$out"
    cat "$out"; echo "saved: $out"; exit 0 ;;
esac
# A running title keeps eboot.bin busy: the upload then fails half way ("550 Text file busy") after the old
# files were already removed (2026-10-08). Refuse while any title (eboot.bin) runs.
# (the listing is read whole before it is searched: grep -q stopping early under pipefail made the check
# fail silently and an install ran over a running app)
processes=$(nc -w 4 "$ps5" 9021 < "$here/tools/pskill/pslist.elf" 2>/dev/null || true)
if [[ ${FORCE:-} != 1 ]] && grep -aq " eboot.bin$" <<<"$processes"; then
    echo "A game or app is running on the PS5 (RPCS3 PS5?): close it first, then install again." >&2
    exit 1
fi
# Replacing the title loses the files the package doesn't have in rpcs3/ (2026-10-06/07): keep RPCS3's
# settings (config.yml) and the list of games with a PS5 home-screen tile (ps5-tiles.txt), put them back after
mkdir -p "$here/results"
stamp=$(date +%Y%m%d-%H%M%S)
kept=()
# The last run's logs are not put back (RPCS3 starts a new one), but saved: an install must not lose a test run
for name in RPCS3.log ps5-title.log; do
    curl -s --max-time 120 "$ftp$remote/rpcs3/$name" -o "$here/results/$stamp-$name" || true
    [[ -s $here/results/$stamp-$name ]] || rm -f "$here/results/$stamp-$name"
done
for name in config.yml ps5-tiles.txt ps5-offered-games.txt ps5-vibration.txt; do
    keep="$here/results/${name%.*}-before-deploy-$stamp.${name##*.}"
    curl -s --max-time 30 "$ftp$remote/rpcs3/$name" -o "$keep" || true
    if [[ -s $keep ]]; then kept+=("$name=$keep"); else rm -f "$keep"; fi
done
# Per-game settings (rpcs3/custom_configs/*.yml) went with a reinstall too (2026-10-08): kept and put back
custom_keep="$here/results/custom_configs-before-deploy-$stamp"
mkdir -p "$custom_keep"
for name in $(curl -s --max-time 30 "$ftp$remote/rpcs3/custom_configs/" | awk '{print $NF}' | grep '\.yml$'); do
    curl -s --max-time 30 "$ftp$remote/rpcs3/custom_configs/$name" -o "$custom_keep/$name"
done
"$here/tools/ps5drop-cli" "$ps5" /data/homebrew "$here/dist/PPSA99303"
# ShadowMountPlus finishes installing a few seconds AFTER the upload and drops those files then (a config.yml put
# back at once was gone again): put them back until they stay for 20 s
for entry in "${kept[@]}"; do
    name=${entry%%=*} keep=${entry#*=}
    stable=0
    for _ in $(seq 1 24); do
        sleep 5
        if curl -s --max-time 30 -o /dev/null "$ftp$remote/rpcs3/$name"; then
            stable=$((stable + 1))
            ((stable >= 4)) && break
        else
            stable=0
            curl -sS -T "$keep" "$ftp$remote/rpcs3/$name" &&
                curl -s "$ftp/" -Q "SITE CHMOD 666 $remote/rpcs3/$name" -o /dev/null &&
                echo "rpcs3/$name put back from $keep"
        fi
    done
done
# Did ShadowMountPlus register the title? (2026-10-07: it looked while eboot.bin was being replaced, failed twice,
# then refused the title until reboot - "cannot start the game or app")
# Read the listing whole before searching: `grep` stopping early makes `curl` get SIGPIPE and, under
# pipefail, this made the deploy script exit 1 after a successful upload (skipping the configs restore).
smp_log=$(curl -s --max-time 30 "$ftp/data/shadowmount/debug.log" || true)
smp=$(grep -a "PPSA99303" <<<"$smp_log" | tail -3 || true)
if grep -q "retry limit reached\|nullfs mount failed\|eboot.bin missing" <<<"$smp"; then
    echo "WARNING: ShadowMountPlus did not register PPSA99303:"; echo "$smp"
    echo "  (if /user/app/PPSA99303/mount.lnk is missing, put back the text /data/homebrew/PPSA99303 there)"
fi
for file in "$custom_keep"/*.yml; do
    [[ -f $file ]] || continue
    name=$(basename "$file")
    for _ in $(seq 1 12); do
        if curl -s --max-time 30 -o /dev/null "$ftp$remote/rpcs3/custom_configs/$name"; then break; fi
        curl -sS --ftp-create-dirs -T "$file" "$ftp$remote/rpcs3/custom_configs/$name" &&
            curl -s "$ftp/" -Q "SITE CHMOD 777 $remote/rpcs3/custom_configs" -Q "SITE CHMOD 666 $remote/rpcs3/custom_configs/$name" -o /dev/null &&
            echo "rpcs3/custom_configs/$name put back"
        sleep 5
    done
done
# Loudly flag an uncapped per-game config restored by this install: at Clocks scale != 100 or
# Frame limit Off a game runs ~3x speed (the benchmark setting), which is easy to mistake for a bug.
for file in "$custom_keep"/*.yml; do
    [[ -f $file ]] || continue
    cscale=$(sed -n 's/^[[:space:]]*Clocks scale:[[:space:]]*//p' "$file" | head -1)
    flimit=$(sed -n 's/^[[:space:]]*Frame limit:[[:space:]]*//p' "$file" | head -1)
    if [[ ${cscale:-100} != 100 || ${flimit:-Auto} == Off ]]; then
        echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" >&2
        echo "WARNING: $(basename "$file") has Clocks scale ${cscale:-?} and Frame limit ${flimit:-?}." >&2
        echo "         This is the UNCAPPED BENCHMARK config; the game runs ~3x speed and FPS is not" >&2
        echo "         comparable to normal play. Restore tools/gta4-play-config.yml (Clocks 100 /" >&2
        echo "         Frame limit Auto) before testing or playing normally." >&2
        echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!" >&2
    fi
done
if [[ ${1:-} == --probe ]]; then
    printf 'all\n' | curl -sS -T - "$ftp$remote/platform-probe.txt"
    echo "probe requested: launch RPCS3 PS5 from the home screen, then: tools/deploy-title.sh --results"
fi
