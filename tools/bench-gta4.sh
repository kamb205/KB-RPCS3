#!/usr/bin/env bash
# Benchmark GTA IV (BLES01128) on the PS5: choose SPU Block Size, boot the game, drive a fixed
# sequence into the heavy scene, then report average FPS, 1% low and frame-time percentiles from
# RPCS3.log's "PS5 FPS: ... | frame p50/p95/p99 ..." lines (frame-stat instrumentation, patch 0045).
#
#   PS5_IP=YOUR_PS5_IP tools/bench-gta4.sh safe           # measure with SPU Block Size: Safe
#   PS5_IP=YOUR_PS5_IP tools/bench-gta4.sh mega warm      # build/warm the Mega cache, no measurement
#   PS5_IP=YOUR_PS5_IP tools/bench-gta4.sh mega           # measure with Mega (cache must be warm)
#   PS5_IP=YOUR_PS5_IP tools/bench-gta4.sh kill           # close the running game
#
# The scene is only approximately reproducible, so every measurement runs the same fixed input
# sequence and reports the distribution (min/avg/median), not a single number.
#
# Normal speed is the default: the per-game config is uploaded as Clocks scale 100 / Frame limit Auto
# (tools/gta4-play-config.yml) and restored on exit. The 300%/Off config exposes CPU cost but the game
# runs ~3x speed, so it is used only with UNCAPPED=1 (tools/gta4-bench-config-UNCAPPED-300pct.yml).
# Every measurement is gated on the runtime PS5 SPEED / PS5 TIMING lines: a window whose observed
# clocks/frame-limit or guest/host ratio does not match the intended speed is reported INVALID.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ip=${PS5_IP:?set PS5_IP}
remote=/data/homebrew/PPSA99303
game="/app0/rpcs3/games/GTA 4"
mode=${1:?usage: safe|mega|kill [warm|label]}
arg2=${2:-}
ftp="ftp://$ip:1337"
play_cfg="$here/tools/gta4-play-config.yml"
bench_cfg="$here/tools/gta4-bench-config-UNCAPPED-300pct.yml"
cfg_dst="$remote/rpcs3/custom_configs/config_BLES01128.yml"

# Normal speed is the default. The 300%/Off benchmark config exposes CPU cost but the game runs ~3x
# speed, so it must be opted into explicitly with UNCAPPED=1; the results are then labelled UNCAPPED.
uncapped=${UNCAPPED:-0}
if [[ $uncapped == 1 ]]; then
    base_cfg="$bench_cfg"; want_ratio=3.00; speed_label="UNCAPPED 300%/Off"
else
    base_cfg="$play_cfg";  want_ratio=1.00; speed_label="normal 100%/Auto"
fi

pkill_elf="$here/results/tools/pkill-eboot.elf"
build_pkill() {
    [[ -s $pkill_elf ]] && return
    mkdir -p "$(dirname "$pkill_elf")"
    limactl shell ps5build -- bash -lc \
        "\$HOME/work/ps5-rpcs3-app/.deps/native/ps5-payload-sdk/bin/prospero-clang -O2 -DTARGET=\\\"eboot\\\" -o /tmp/pkill-eboot.elf '$here/tools/pskill/main.c'" >/dev/null
    limactl copy ps5build:/tmp/pkill-eboot.elf "$pkill_elf" >/dev/null
}

close_game() {
    build_pkill
    # Read the listing whole before searching: grep -q stopping early makes nc get SIGPIPE and, under
    # pipefail, the check silently fails (same trap as deploy-title.sh).
    local listing
    listing=$(nc -w 4 "$ip" 9021 < "$here/tools/pskill/pslist.elf" 2>/dev/null || true)
    if grep -aq ' eboot.bin$' <<<"$listing"; then
        nc -w 6 "$ip" 9021 < "$pkill_elf" >/dev/null 2>&1 || true
        sleep 4
    fi
}

log_lines() {
    local n i
    for i in 1 2 3 4 5; do
        n=$(curl -s --max-time 30 "$ftp$remote/rpcs3/RPCS3.log" | wc -l | tr -d ' ')
        [[ ${n:-0} -gt 0 ]] && { echo "$n"; return 0; }
        sleep 5
    done
    echo 0
}

set_block() {
    local block=$1 base=$base_cfg
    [[ -f $base ]] || { echo "missing $base" >&2; exit 1; }
    # Refuse to run the UNCAPPED config without the explicit opt-in (defence in depth).
    local cscale flimit
    cscale=$(sed -n 's/^[[:space:]]*Clocks scale:[[:space:]]*//p' "$base" | head -1)
    flimit=$(sed -n 's/^[[:space:]]*Frame limit:[[:space:]]*//p' "$base" | head -1)
    if [[ ${cscale:-100} != 100 || ${flimit:-Auto} == Off ]] && [[ $uncapped != 1 ]]; then
        echo "refusing to upload an uncapped config ($base: clocks ${cscale:-?}, frame ${flimit:-?}); set UNCAPPED=1" >&2
        exit 1
    fi
    # The console's Wi-Fi drops intermittently: retry the upload a few times.
    local attempt ok=
    for attempt in 1 2 3 4 5; do
        if sed -E "s/^  SPU Block Size: .*/  SPU Block Size: $block/" "$base" \
            | curl -sS --ftp-create-dirs --max-time 30 -T - "$ftp$remote/rpcs3/custom_configs/config_BLES01128.yml"; then
            ok=1; break
        fi
        echo "config upload attempt $attempt failed; retrying..." >&2; sleep 10
    done
    [[ $ok ]] || { echo "config upload failed" >&2; exit 1; }
    # The title owns its files and reads them regardless; the CHMOD only helps FTP. Never fatal.
    curl -s "$ftp/" -Q "SITE CHMOD 666 $remote/rpcs3/custom_configs/config_BLES01128.yml" -o /dev/null || true
    echo "SPU Block Size: $block ($speed_label)"
}

# Put the normal-play config (Clocks 100 / Frame limit Auto) back, with a byte-exact read-back check.
# Without this a benchmark leaves the console at 300% and games run ~3x speed until it is noticed.
restore_play_config() {
    local a tmp
    [[ -f $play_cfg ]] || { echo "WARNING: $play_cfg missing — cannot restore normal play config" >&2; return 1; }
    tmp=$(mktemp)
    for a in 1 2 3 4 5; do
        if curl -sS --ftp-create-dirs --max-time 30 -T "$play_cfg" "$ftp$cfg_dst" \
            && curl -s --max-time 30 "$ftp$cfg_dst" -o "$tmp" \
            && cmp -s "$tmp" "$play_cfg"; then
            rm -f "$tmp"
            curl -s "$ftp/" -Q "SITE CHMOD 666 $cfg_dst" -o /dev/null || true
            echo "restored normal-play config (Clocks scale 100, Frame limit Auto)"
            return 0
        fi
        echo "play-config restore attempt $a failed; retrying..." >&2; sleep 5
    done
    rm -f "$tmp"
    echo "WARNING: normal-play config restore NOT confirmed — console may be left at benchmark speed" >&2
    return 1
}

# The fixed route from the title screen into gameplay (validated 2026-10-08). GTA IV caps at 30 fps,
# so a light scene cannot show CPU cost; the route advances the opening and walks into traffic.
drive_sequence() {
    sleep 45
    PS5_IP=$ip "$here/tools/ps5-input.sh" "hold OPTIONS 1.0" >/dev/null; sleep 10
    PS5_IP=$ip "$here/tools/ps5-input.sh" "hold CROSS 1.0"   >/dev/null; sleep 10
    PS5_IP=$ip "$here/tools/ps5-input.sh" "hold DOWN 0.3"    >/dev/null; sleep 2
    PS5_IP=$ip "$here/tools/ps5-input.sh" "hold CROSS 1.0"   >/dev/null; sleep 15
    for _ in 1 2 3 4; do
        PS5_IP=$ip "$here/tools/ps5-input.sh" "hold CROSS 0.4" >/dev/null; sleep 10
    done
    PS5_IP=$ip "$here/tools/ps5-input.sh" "stick 0 1 0 0 8.0" >/dev/null
    sleep 15
}

# Measure the running session (no relaunch): the user positions the character, then we sample.
measure_running() {
    local label=$1
    echo "measuring the running session for 30 s..."
    local mark; mark=$(log_lines)                     # ignore FPS lines before this
    sleep 30
    local log; log=$(ls -t "$here"/results/*RPCS3.log | head -1)
    PS5_IP=$ip "$here/tools/deploy-title.sh" --logs >/dev/null 2>&1 || true
    log=$(ls -t "$here"/results/*RPCS3.log | head -1)
    local out="$here/results/bench/${label}-$(date +%Y%m%d-%H%M%S)"
    mkdir -p "$out"
    cp "$log" "$out/RPCS3.log"
    python3 - "$log" "$mark" "$label" "$out" "$want_ratio" <<'PY'
import re, statistics, sys, json
log, mark, label, out, want = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], float(sys.argv[5])
lines = open(log, encoding='utf-8', errors='replace').read().splitlines()[mark:]
fps, p50, p95, p99, low, cpu, ratios, vblank = [], [], [], [], [], [], [], []
speed = None
for l in lines:
    m = re.search(r'PS5 FPS: ([0-9.]+) \| frame p50 ([0-9.]+) p95 ([0-9.]+) p99 ([0-9.]+) ms, 1% low ([0-9.]+) fps', l)
    if m:
        fps.append(float(m.group(1))); p50.append(float(m.group(2)))
        p95.append(float(m.group(3))); p99.append(float(m.group(4))); low.append(float(m.group(5)))
    c = re.search(r'PS5 CPU: total (\d+)%', l)
    if c:
        cpu.append(int(c.group(1)))
    s = re.search(r'PS5 SPEED: clocks scale (\d+)%, frame limit (\S+), vblank (\d+) Hz', l)
    if s:
        speed = (int(s.group(1)), s.group(2))
    t = re.search(r'PS5 TIMING: guest/host ([0-9.]+), vblank ([0-9.]+)/s', l)
    if t:
        ratios.append(float(t.group(1))); vblank.append(float(t.group(2)))
res = {}
if len(fps) >= 2:
    res = dict(windows=len(fps), fps_avg=round(statistics.mean(fps),1),
               fps_median=round(statistics.median(fps),1), fps_min=round(min(fps),1), fps_max=round(max(fps),1),
               frame_p50_ms=round(statistics.median(p50),2), frame_p95_ms=round(statistics.median(p95),2),
               frame_p99_ms=round(statistics.median(p99),2), low1pct_avg=round(statistics.mean(low),1),
               cpu_total_avg=(round(statistics.mean(cpu)) if cpu else None),
               cpu_total_min=(min(cpu) if cpu else None))
# Validity gate: the window is only a valid measurement of the intended speed if the observed PS5 SPEED
# matches and PS5 TIMING's guest/host ratio is the expected one. Rejects the old 300%-config logs.
exp_clocks = 300 if want > 1.5 else 100
exp_frame = 'Off' if want > 1.5 else 'Auto'
ratio_med = statistics.median(ratios) if ratios else None
vblank_med = statistics.median(vblank) if vblank else None
reasons = []
if speed is None:
    reasons.append('no PS5 SPEED line')
else:
    if speed[0] != exp_clocks: reasons.append('clocks %d%% != %d%%' % (speed[0], exp_clocks))
    if speed[1] != exp_frame:  reasons.append('frame limit %s != %s' % (speed[1], exp_frame))
if ratio_med is None:
    reasons.append('no PS5 TIMING lines')
elif abs(ratio_med - want) > 0.05:
    reasons.append('guest/host %.3f != %.2f' % (ratio_med, want))
if vblank_med is not None and not (50 <= vblank_med <= 70):
    reasons.append('vblank %.1f/s outside 50-70' % vblank_med)
valid = not reasons and len(fps) >= 2
res.update(dict(valid=valid, speed=speed, guest_host_median=ratio_med, vblank_s_median=vblank_med,
                invalid_reasons=reasons))
print(json.dumps({label: res}, indent=2))
json.dump(res, open(out + '/summary.json', 'w'), indent=2)
if not valid:
    print("WARNING: window '%s' is INVALID for the intended speed (guest/host %.2f): %s" % (label, want, "; ".join(reasons) or "too few FPS windows"))
PY
    echo "saved: $out"
}

# Every mode that starts and finishes its own measurement restores normal play speed on the way out.
# 'position' is intentionally excluded: it hands a running game in the UNCAPPED config to the follow-up
# 'current', which restores on its own exit.
if [[ $mode != position ]]; then
    trap 'restore_play_config || true' EXIT INT TERM
fi

case "$mode" in
kill) close_game; echo "game closed" ;;
current) measure_running "${arg2:-current}" ;;
position)
    case "${arg2:-safe}" in safe|Safe) b=Safe ;; mega|Mega) b=Mega ;; *) echo "position safe|mega" >&2; exit 1 ;; esac
    set_block "$b"; close_game; PS5_IP=$ip "$here/tools/ps5-launch.sh" "$game" | tail -1; drive_sequence
    echo "positioned ($b). Put the character in the heavy spot, then: tools/bench-gta4.sh current $b-heavy"
    if [[ $uncapped == 1 ]]; then
        echo "NOTE: the console is left at Clocks scale 300 / Frame limit Off (UNCAPPED=1) for that measurement;"
        echo "      tools/bench-gta4.sh current restores the normal-play config when it exits."
    else
        echo "NOTE: positioned at NORMAL speed (Clocks 100 / Frame limit Auto)."
    fi ;;
safe) set_block Safe; close_game; PS5_IP=$ip "$here/tools/ps5-launch.sh" "$game" | tail -1; drive_sequence; measure_running "safe${arg2:+-$arg2}" ;;
mega)
    if [[ $arg2 == warm ]]; then
        set_block Mega; close_game; PS5_IP=$ip "$here/tools/ps5-launch.sh" "$game" | tail -1
        echo "warming the Mega SPU cache; leaving it on the heavy scene for ~4 minutes..."
        drive_sequence; sleep 240; close_game; echo "Mega cache warm"
    else
        set_block Mega; close_game; PS5_IP=$ip "$here/tools/ps5-launch.sh" "$game" | tail -1; drive_sequence; measure_running "mega${arg2:+-$arg2}"
    fi ;;
*) echo "usage: safe|mega|current|kill [warm|label]" >&2; exit 1 ;;
esac
