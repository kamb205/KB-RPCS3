#!/usr/bin/env bash
# Live A/B of the GPU-wait spin/backoff (RPCS3 patch 0046), without relaunching the game:
#   A = empty rpcs3/ps5-tuning.txt  (today's behaviour: RADV spins 4 ms, vk spins unbounded)
#   B = radv_spin_us 200 / vk_event_spin_us 30   (sleeps stay 100 us)
# Runs A,B,A,B,A,B in the SAME heavy scene, 32 s windows, and reports FPS / frame p50,p99 / CPU / SPU sum /
# the PS5 WAITS counters. The game must already be positioned in the heavy spot (tools/bench-gta4.sh position).
# Normal speed is the default; position the scene with UNCAPPED=1 for the 300% run and set UNCAPPED=1 here too.
# On exit the normal-play config is restored. A window whose observed PS5 SPEED / PS5 TIMING do not match the
# intended speed is marked invalid.
#   PS5_IP=YOUR_PS5_IP tools/bench-ab.sh
set -uo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ip=${PS5_IP:?set PS5_IP}
remote=/data/homebrew/PPSA99303
ftp="ftp://$ip:1337"
tmp="$remote/rpcs3/ps5-tuning.tmp"
dst="$remote/rpcs3/ps5-tuning.txt"
play_cfg="$here/tools/gta4-play-config.yml"
cfg_dst="$remote/rpcs3/custom_configs/config_BLES01128.yml"
# Normal speed by default; the 300%/Off window is opted into with UNCAPPED=1. The validity gate below
# rejects a window whose observed PS5 SPEED / PS5 TIMING do not match the intended speed.
uncapped=${UNCAPPED:-0}
if [[ $uncapped == 1 ]]; then want_ratio=3.00; else want_ratio=1.00; fi
out="$here/results/bench/ab-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$out"

# Put the normal-play config (Clocks 100 / Frame limit Auto) back, with a byte-exact read-back check.
restore_play_config() {
    local a t
    [[ -f $play_cfg ]] || { echo "WARNING: $play_cfg missing — cannot restore normal play config" >&2; return 1; }
    t=$(mktemp)
    for a in 1 2 3 4 5; do
        if curl -sS --ftp-create-dirs --max-time 30 -T "$play_cfg" "$ftp$cfg_dst" \
            && curl -s --max-time 30 "$ftp$cfg_dst" -o "$t" \
            && cmp -s "$t" "$play_cfg"; then
            rm -f "$t"
            curl -s "$ftp/" -Q "SITE CHMOD 666 $cfg_dst" -o /dev/null || true
            echo "restored normal-play config (Clocks scale 100, Frame limit Auto)"
            return 0
        fi
        echo "play-config restore attempt $a failed; retrying..." >&2; sleep 5
    done
    rm -f "$t"
    echo "WARNING: normal-play config restore NOT confirmed — console may be left at benchmark speed" >&2
    return 1
}

trap 'restore_play_config || true' EXIT INT TERM

put_tuning() {
    local content=$1 a got
    for a in 1 2 3 4 5; do
        # The rename can fail on the flaky Wi-Fi and would leave the wrong tuning in force: retry both together.
        if printf '%s' "$content" | curl -sS --max-time 30 --ftp-create-dirs -T - "$ftp$tmp" \
            && curl -s "$ftp/" -Q "RNFR $tmp" -Q "RNTO $dst" -o /dev/null; then
            break
        fi
        echo "tuning upload/rename retry $a" >&2; sleep 5
    done
    curl -s "$ftp/" -Q "SITE CHMOD 666 $dst" -o /dev/null || true
    # $(...) strips trailing newlines, so compare with the content's trailing newlines stripped too.
    local want="${content%$'\n'}"
    got=$(curl -s --max-time 30 "$ftp$dst")
    if [[ "$got" != "$want" ]]; then
        echo "FATAL: tuning file did not match after upload." >&2
        echo "  expected: [$want]" >&2
        echo "  got:      [$got]" >&2
        exit 1
    fi
}

log_lines() {
    local n a
    for a in 1 2 3 4 5; do
        n=$(curl -s --max-time 30 "$ftp$remote/rpcs3/RPCS3.log" | wc -l | tr -d ' ')
        [[ ${n:-0} -gt 0 ]] && { echo "$n"; return 0; }
        sleep 5
    done
    echo 0
}

# One 32 s window: mark the live log, wait, fetch, parse the lines after the mark.
run_window() {
    local label=$1 mark log
    mark=$(log_lines)
    sleep 32
    log="$out/$label.log"
    local a
    for a in 1 2 3 4 5; do
        if curl -s --max-time 60 "$ftp$remote/rpcs3/RPCS3.log" -o "$log" && [[ -s $log ]]; then break; fi
        sleep 5
    done
    python3 - "$log" "$mark" "$label" "$out/summary.json" "$want_ratio" <<'PY'
import json, re, statistics, sys
log, mark, label, summary, want = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], float(sys.argv[5])
all_lines = open(log, encoding='utf-8', errors='replace').read().splitlines()
lines = all_lines[mark:]
F, P50, P99, C, S = [], [], [], [], []
waits = None
ratios, vblank = [], []
speed = None
for l in lines:
    m = re.search(r'PS5 FPS: ([0-9.]+) \| frame p50 ([0-9.]+) p95 [0-9.]+ p99 ([0-9.]+) ms', l)
    if m:
        F.append(float(m.group(1))); P50.append(float(m.group(2))); P99.append(float(m.group(3)))
    c = re.search(r'PS5 CPU: total (\d+)%', l)
    if c:
        C.append(int(c.group(1)))
        S.append(sum(int(x) for x in re.findall(r'SPU\[[^\]]+\]\s+\S+\s+(\d+)%', l)))
    w = re.search(r'PS5 WAITS: radv (\d+)/s spin (\d+) ms/s sleeps (\d+)/s wait (\d+) ms/s \| vkevent (\d+)/s spin (\d+) ms/s sleeps (\d+)/s', l)
    if w:
        waits = dict(radv_waits_s=int(w.group(1)), radv_spin_ms_s=int(w.group(2)), radv_sleeps_s=int(w.group(3)),
                     radv_wait_ms_s=int(w.group(4)), vk_waits_s=int(w.group(5)), vk_spin_ms_s=int(w.group(6)),
                     vk_sleeps_s=int(w.group(7)))
    s = re.search(r'PS5 SPEED: clocks scale (\d+)%, frame limit (\S+), vblank (\d+) Hz', l)
    if s:
        speed = (int(s.group(1)), s.group(2))
    t = re.search(r'PS5 TIMING: guest/host ([0-9.]+), vblank ([0-9.]+)/s', l)
    if t:
        ratios.append(float(t.group(1))); vblank.append(float(t.group(2)))
# The tuning marker in force for this window: the last "PS5 TUNING:" at or before the window's last FPS line
# (the marker is logged right after the file changes, i.e. before the window's mark, so search the whole log).
last_fps = max((i for i, l in enumerate(lines) if 'PS5 FPS:' in l), default=-1)
last_fps_global = mark + last_fps if last_fps >= 0 else len(all_lines) - 1
tuning = None
for l in reversed(all_lines[:last_fps_global + 1]):
    m = re.search(r'PS5 TUNING: (.*)$', l)
    if m:
        tuning = m.group(1).strip()
        break
tuning_valid = None
if tuning is not None:
    if label.startswith('A'):
        tuning_valid = ('radv_spin_us=4000' in tuning) and ('vk_event_spin_us=-1' in tuning)
    elif label.startswith('B'):
        tuning_valid = ('radv_spin_us=200' in tuning) and ('vk_event_spin_us=30' in tuning)
# Speed validity: the observed PS5 SPEED / PS5 TIMING must match the intended speed.
exp_clocks = 300 if want > 1.5 else 100
exp_frame = 'Off' if want > 1.5 else 'Auto'
speed_reasons = []
if speed is None:
    speed_reasons.append('no PS5 SPEED line')
else:
    if speed[0] != exp_clocks: speed_reasons.append('clocks %d%% != %d%%' % (speed[0], exp_clocks))
    if speed[1] != exp_frame:  speed_reasons.append('frame limit %s != %s' % (speed[1], exp_frame))
ratio_med = statistics.median(ratios) if ratios else None
if ratio_med is None:
    speed_reasons.append('no PS5 TIMING lines')
elif abs(ratio_med - want) > 0.05:
    speed_reasons.append('guest/host %.3f != %.2f' % (ratio_med, want))
speed_valid = not speed_reasons
valid = bool(tuning_valid) and speed_valid

r = dict(label=label, windows=len(F), valid=valid, tuning=tuning,
         speed_valid=speed_valid, speed=speed, guest_host_median=ratio_med, invalid_reasons=speed_reasons,
         fps_avg=round(statistics.mean(F), 1) if F else None,
         fps_min=round(min(F), 1) if F else None,
         frame_p50_ms=round(statistics.median(P50), 2) if P50 else None,
         frame_p99_ms=round(statistics.median(P99), 2) if P99 else None,
         cpu_total_avg=round(statistics.mean(C)) if C else None,
         spu_sum_avg=round(statistics.mean(S)) if S else None)
if waits:
    r.update(waits)
data = {}
try:
    data = json.load(open(summary))
except Exception:
    pass
data[label] = r
json.dump(data, open(summary, 'w'), indent=2)
print(json.dumps({label: r}, indent=2))
if not valid:
    print("WARNING: window '%s' is INVALID: tuning_valid=%s speed_reasons=%s" % (label, tuning_valid, speed_reasons))
PY
}

echo "output: $out"
for cycle in 1 2 3; do
    echo "=== A$cycle (defaults) ==="
    put_tuning ""
    sleep 8; run_window "A$cycle"
    echo "=== B$cycle (radv_spin 200us, vk_spin 30us) ==="
    put_tuning "radv_spin_us 200
vk_event_spin_us 30
"
    sleep 8; run_window "B$cycle"
done
echo "done: $out/summary.json"
