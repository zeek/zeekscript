#!/usr/bin/env python3
"""Extract all formatting test cases into individual files.

Instruments the _format method to capture every (input, output) pair
produced during test execution, then writes them to numbered files.

If input != output: test001.zeek (raw) + test001.fmt.zeek (formatted)
If input == output: test001.zeek only (already canonical)
"""

import io
import os
import sys
import unittest

# Add the tests directory to sys.path so we can import testutils
tests_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, tests_dir)

import testutils as tu
from testutils import zeekscript

# Collect all (input_bytes, output_bytes, test_name) triples
collected = []
current_test_name = ""


def capturing_format(self, content):
    """Replacement for TestFormatting._format that records pairs."""
    script = zeekscript.Script(io.BytesIO(content))
    self.assertTrue(script.parse())
    self.assertFalse(script.has_error())
    buf = io.BytesIO()
    script.format(buf)
    output = buf.getvalue()
    collected.append((content, output, current_test_name))
    return output


def main():
    global current_test_name

    from test_formatting import TestFormatting

    # Monkey-patch _format
    original_format = TestFormatting._format
    TestFormatting._format = capturing_format

    # Discover and run all tests, capturing pairs
    loader = unittest.TestLoader()
    suite = loader.loadTestsFromTestCase(TestFormatting)

    # Run each test individually so we can track the test name
    for test in suite:
        current_test_name = test._testMethodName
        try:
            test.debug()  # runs without catching exceptions
        except Exception as e:
            print(f"  SKIP {current_test_name}: {e}", file=sys.stderr)

    # Restore
    TestFormatting._format = original_format

    # Write to output directory
    outdir = os.path.join(tests_dir, "formatting")
    os.makedirs(outdir, exist_ok=True)

    # Also create an index file mapping test number -> test name
    index_lines = []

    for i, (raw, fmt, name) in enumerate(collected, 1):
        tag = f"test{i:03d}"

        # Normalize: strip trailing whitespace from both for comparison
        raw_stripped = raw.rstrip()
        fmt_stripped = fmt.rstrip()

        if raw_stripped != fmt_stripped:
            # Two files: raw input and formatted output
            with open(os.path.join(outdir, f"{tag}.zeek"), "wb") as f:
                f.write(raw)
            with open(os.path.join(outdir, f"{tag}.fmt.zeek"), "wb") as f:
                f.write(fmt)
            index_lines.append(f"{tag}  {name}  (raw + formatted)")
        else:
            # Already canonical — one file
            with open(os.path.join(outdir, f"{tag}.zeek"), "wb") as f:
                f.write(fmt)
            index_lines.append(f"{tag}  {name}  (canonical)")

    # Write index
    with open(os.path.join(outdir, "INDEX"), "w") as f:
        f.write("\n".join(index_lines) + "\n")

    print(f"Extracted {len(collected)} test cases to {outdir}/")
    print(f"  Two-file (raw + fmt): {sum(1 for r, f, _ in collected if r.rstrip() != f.rstrip())}")
    print(f"  Single-file (canonical): {sum(1 for r, f, _ in collected if r.rstrip() == f.rstrip())}")


if __name__ == "__main__":
    main()
