#!/usr/bin/env bash
set -euo pipefail

if ! command -v ffprobe >/dev/null 2>&1; then
  echo "ffprobe not found. Please install ffmpeg/ffprobe." >&2
  exit 1
fi

MANUAL_BPM=""
files=()
while [ $# -gt 0 ]; do
  case "$1" in
    --bpm)
      MANUAL_BPM="$2"
      shift 2
      ;;
    *)
      files+=("$1")
      shift
      ;;
  esac
done

if [ ${#files[@]} -lt 1 ]; then
  echo "Usage: $0 [--bpm <value>] <audio-file> [more files...]" >&2
  exit 1
fi

for f in "${files[@]}"; do
  if [ ! -f "$f" ]; then
    echo "File not found: $f" >&2
    continue
  fi
  echo "=== $f ==="
  duration=$(ffprobe -v error -show_entries format=duration \
              -of default=noprint_wrappers=1:nokey=1 "$f")
  samplerate=$(ffprobe -v error -select_streams a:0 \
                -show_entries stream=sample_rate \
                -of default=noprint_wrappers=1:nokey=1 "$f")
  frames=$(ffprobe -v error -select_streams a:0 \
              -show_entries stream=nb_frames \
              -of default=noprint_wrappers=1:nokey=1 "$f" 2>/dev/null || echo "?")

  echo "duration=${duration}s"
  echo "sample_rate=${samplerate} Hz"
  echo "frames=${frames}"

  tbpm=$(ffprobe -v error -show_entries stream_tags=TBPM:format_tags=TBPM,bpm \
           -of default=noprint_wrappers=1:nokey=1 "$f" | head -n1 || true)
  bpm_val=${MANUAL_BPM:-$tbpm}
  if [ -n "$bpm_val" ]; then
    echo "BPM=${bpm_val} (manual override or tag)"
    python3 - <<'PY' "$duration" "$bpm_val"
import sys
dur=float(sys.argv[1]); bpm=float(sys.argv[2])
beat=60.0/bpm
bars=[1,2,4,8,16,32]
print("\nSuggested loop_end (seconds) for whole-bar lengths (4/4):")
for b in bars:
    le=b*4*beat
    print(f"  {b:>2} bars: {le:.3f}s (frames @44.1kHz: {int(le*44100)})")
PY
  else
    echo "BPM not provided and no tag found (use --bpm to override)"
  fi

  # Raw ffprobe dump for reference
  ffprobe -v error \
    -show_entries format=duration:stream=sample_rate,nb_frames \
    -of default=noprint_wrappers=1 "$f"
  ffprobe -v error -show_entries stream_tags=TBPM:format_tags=TBPM,bpm \
    -of default=noprint_wrappers=1:nokey=0 "$f" || true
  echo
done
