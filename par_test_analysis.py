#!/usr/bin/env python3
"""
par_test_analysis.py  —  Parse PAR accuracy-test UART log and compute metrics.

Usage:
    python par_test_analysis.py  putty.log

The log must contain a block bounded by:
    === PAR TEST BEGIN ===
    ...
    === PAR TEST END ===

Each data line inside that block has the format:
    phase,window,predicted,confidence_pct
where phase and predicted are 0-3 (STATIONARY / WALKING / CLIMBING / LAYING).
"""

import sys
import os

# ── Constants ─────────────────────────────────────────────────────────────────
CLASS_NAMES  = ["STATIONARY", "WALKING", "CLIMBING", "LAYING"]
N_CLASSES    = len(CLASS_NAMES)
SETTLE_WIN   = 3   # skip first N windows of each phase (settling / vote warm-up)


# ── Log parser ────────────────────────────────────────────────────────────────
def parse_log(path):
    """Return list of (phase, window, predicted, confidence_pct) tuples."""
    records = []
    in_block = False

    with open(path, "r", encoding="utf-8", errors="ignore") as fh:
        for raw in fh:
            line = raw.strip()
            if "=== PAR TEST BEGIN ===" in line:
                in_block = True
                continue
            if "=== PAR TEST END ===" in line:
                in_block = False
                continue
            if not in_block:
                continue
            if line.startswith("#") or line.startswith("phase") or not line:
                continue

            parts = line.split(",")
            if len(parts) != 4:
                continue
            try:
                phase  = int(parts[0])
                window = int(parts[1])
                pred   = int(parts[2])
                conf   = int(parts[3])
                records.append((phase, window, pred, conf))
            except ValueError:
                continue

    return records


# ── Analysis ──────────────────────────────────────────────────────────────────
def analyse(records):
    # Filter out settling windows
    valid = [(ph, win, pred, conf)
             for ph, win, pred, conf in records
             if win > SETTLE_WIN]

    if not valid:
        print("[ERROR] No valid records found after filtering settling windows.")
        return

    # Build confusion matrix and collect per-class confidence
    confusion  = [[0] * N_CLASSES for _ in range(N_CLASSES)]
    conf_lists = [[] for _ in range(N_CLASSES)]

    for ph, win, pred, conf in valid:
        if 0 <= ph < N_CLASSES and 0 <= pred < N_CLASSES:
            confusion[ph][pred] += 1
            if ph == pred:
                conf_lists[ph].append(conf)

    # ── Header ────────────────────────────────────────────────────────────────
    sep = "=" * 60
    print(f"\n{sep}")
    print("  PAR System — Accuracy Test Results")
    print(f"  IIT Jodhpur  |  EEP3020 Digital Systems Lab")
    print(sep)
    print(f"\n  System parameters")
    print(f"    Sample rate   : 50 Hz")
    print(f"    Window length : 128 samples  (2.56 s)")
    print(f"    Hop length    : 64 samples   (1.28 s)")
    print(f"    Vote buffer   : 3 windows")
    print(f"    BODY_ACC_GAIN : 6.0 x   CLIP : 0.5 g")
    print(f"\n  Test parameters")
    print(f"    Duration/class : 30 s")
    print(f"    Settling skip  : first {SETTLE_WIN} windows per phase")
    print(f"    Valid records  : {len(valid)}  (raw: {len(records)})\n")

    # ── Per-class accuracy ────────────────────────────────────────────────────
    print(f"  {'Class':<14}  {'Correct':>7}  {'Total':>7}  {'Accuracy':>9}  {'Mean conf':>10}")
    print("  " + "-" * 52)

    total_correct = 0
    total_count   = 0

    for c in range(N_CLASSES):
        row_sum = sum(confusion[c])
        correct = confusion[c][c]
        acc     = (correct / row_sum * 100.0) if row_sum else 0.0
        mean_c  = (sum(conf_lists[c]) / len(conf_lists[c])) if conf_lists[c] else 0.0
        total_correct += correct
        total_count   += row_sum
        print(f"  {CLASS_NAMES[c]:<14}  {correct:>7}  {row_sum:>7}  {acc:>8.1f}%  {mean_c:>9.1f}%")

    overall = (total_correct / total_count * 100.0) if total_count else 0.0
    print("  " + "-" * 52)
    print(f"  {'OVERALL':<14}  {total_correct:>7}  {total_count:>7}  {overall:>8.1f}%\n")

    # ── Confusion matrix ──────────────────────────────────────────────────────
    col_w = 12
    print("  Confusion matrix   (rows = true class,  cols = predicted class)")
    print("  Diagonal = correct classifications  (* marker)\n")

    # Header row
    hdr = "  " + " " * 14
    for name in CLASS_NAMES:
        hdr += f"{name[:col_w]:>{col_w}}"
    print(hdr)
    print("  " + "-" * (14 + col_w * N_CLASSES))

    for r in range(N_CLASSES):
        row_sum = sum(confusion[r])
        row_str = f"  {CLASS_NAMES[r]:<14}"
        for c in range(N_CLASSES):
            pct  = (confusion[r][c] / row_sum * 100.0) if row_sum else 0.0
            cell = f"{confusion[r][c]}({'*' if r==c else ' '}){pct:.0f}%"
            row_str += f"{cell:>{col_w}}"
        print(row_str)

    print()

    # ── Timing ────────────────────────────────────────────────────────────────
    hop_ms    = 64 * 1000 // 50          # 1280 ms
    settle_ms = SETTLE_WIN * hop_ms      # 3840 ms
    print(f"  Response time  : {hop_ms} ms per window")
    print(f"  Settling time  : ~{settle_ms/1000:.1f} s  "
          f"(vote buffer fills after {SETTLE_WIN} windows)")
    print()
    print(sep)


# ── Entry point ───────────────────────────────────────────────────────────────
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    log_path = sys.argv[1]
    if not os.path.isfile(log_path):
        print(f"[ERROR] File not found: {log_path}")
        sys.exit(1)

    records = parse_log(log_path)
    if not records:
        print("[ERROR] No PAR test data found. "
              "Check that the log contains === PAR TEST BEGIN/END ===.")
        sys.exit(1)

    print(f"Parsed {len(records)} raw prediction records.")
    analyse(records)
