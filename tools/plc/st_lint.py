#!/usr/bin/env python3
"""Structural lint for the IEC 61131-3 Structured Text sources.

No vendor compiler is available on the build host, so this script checks
what a compiler would reject first and what a code reader cares about most:

  * every block opener has its closer (IF/END_IF, CASE/END_CASE, ...)
  * every E_INSPECT_STATE enumerator (ST_*) is handled as a CASE label
  * the ST_ERROR entry block resets the outputs that must never stay on

Usage: st_lint.py FILE.st [FILE.st ...]      exit status 1 on any finding
"""
import re
import sys

PAIRS = [
    (r"(?:SAFETY_)?PROGRAM", "END_PROGRAM"),
    ("FUNCTION_BLOCK", "END_FUNCTION_BLOCK"),
    ("IF", "END_IF"),
    ("CASE", "END_CASE"),
    ("FOR", "END_FOR"),
    ("WHILE", "END_WHILE"),
    ("REPEAT", "END_REPEAT"),
    (r"VAR(?:_INPUT|_OUTPUT|_IN_OUT|_TEMP|_GLOBAL|_EXTERNAL|_STAT)?", "END_VAR"),
    ("TYPE", "END_TYPE"),
    ("STRUCT", "END_STRUCT"),
]

# Outputs that the ST_ERROR entry of MAIN_SEQUENCE must drive to FALSE.
REQUIRED_ERROR_RESETS = [
    "VISION_TRIG", "VISION_PENDING", "SOL_SHADE_FWD", "SOL_PLATFORM_VACUUM",
    "SOL_NEST_VACUUM_CCD4", "SOL_NEST_VACUUM_SIDE", "SOL_GRIPPER_LIFT",
    "SOL_CCD1_REACH", "NG_BELT_RUN", "CYCLE_RUNNING", "ROBOT_START",
]


def strip_comments(src):
    src = re.sub(r"\(\*.*?\*\)", "", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    src = re.sub(r"'[^'\n]*'", "''", src)  # string literals
    return src


def count_kw(code, kw):
    return len(re.findall(r"(?<![A-Z0-9_])(?:" + kw + r")(?![A-Z0-9_])", code))


def lint_file(path):
    findings = []
    raw = open(path, encoding="utf-8").read()
    code = strip_comments(raw)
    up = code.upper()

    for opener, closer in PAIRS:
        n_open, n_close = count_kw(up, opener), count_kw(up, closer)
        if n_open != n_close:
            findings.append(f"{opener}={n_open} {closer}={n_close} mismatch")

    enums = set(re.findall(r"\b(ST_[A-Z0-9_]+)\s*:=\s*\d+", code))
    labels = set(re.findall(r"^\s*(ST_[A-Z0-9_]+)\s*:(?!=)", code, flags=re.M))
    missing = sorted(enums - labels)
    if missing:
        findings.append(f"states never handled in CASE: {missing}")

    if re.search(r"\bPROGRAM\s+MAIN_SEQUENCE\b", code):
        # State bodies must wait on AT_TARGET (position-compared), never on the
        # drive's raw IN_POSITION bit, which stays TRUE for a cycle after a new
        # target is written.
        start = code.rfind("CASE CURRENT_STATE OF")
        end = code.rfind("END_CASE")   # the main CASE closes last; the transition block follows
        body = code[start:end] if start >= 0 and end > start else ""
        if not body:
            findings.append("main state machine / transition block anchors not found")
        raw_inp = re.findall(r"AXIS\[[^\]]+\]\.IN_POSITION", body)
        if raw_inp:
            findings.append(f"state bodies read raw IN_POSITION {len(raw_inp)}x (use AT_TARGET)")
        # Pre-CASE error sources must latch ERROR_REQ; the transition block
        # must apply it so a body transition in the same scan cannot win.
        trans = code[end:] if end > 0 else ""
        if not re.search(r"\bERROR_REQ\b", trans):
            findings.append("transition block does not apply ERROR_REQ")

        m = re.search(r"^\s*ST_ERROR\s*:(?!=)(.*?)^\s*END_CASE", code, flags=re.M | re.S)
        if not m:
            findings.append("ST_ERROR block not found")
        else:
            block = m.group(1)
            for name in REQUIRED_ERROR_RESETS:
                if not re.search(r"\b" + name + r"(?:\[[^\]]*\])?\s*:=\s*FALSE\b", block):
                    findings.append(f"ST_ERROR does not reset {name}")
    return findings


def main(paths):
    rc = 0
    for p in paths:
        findings = lint_file(p)
        if findings:
            rc = 1
            print(f"{p}: FAIL")
            for f in findings:
                print(f"  - {f}")
        else:
            print(f"{p}: OK")
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
