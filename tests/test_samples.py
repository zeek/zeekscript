"""Snapshot test of formatting of sample files.

Code samples are located in tests/samples. To add a new sample create a new
file and create a snapshot of the formatting result with

    $ pytest --snapshot-update

Commit both the sample as well as the updated snapshot.
"""

import io
import os
from pathlib import Path

import pytest
from syrupy.assertion import SnapshotAssertion
from syrupy.extensions.single_file import SingleFileSnapshotExtension
from testutils import zeekscript

SAMPLES_DIR = Path(__file__).parent / "samples"


# Use a custom snapshot fixture so we emit one file per generated test case
# instead of one per module.
@pytest.fixture
def snapshot(snapshot: SnapshotAssertion):
    return snapshot.use_extension(SingleFileSnapshotExtension)


def _format(script: zeekscript.Script):
    """Formats a given `Script`"""
    buf = io.BytesIO()
    script.format(buf)
    return buf.getvalue()


def _get_samples() -> list[Path]:
    """Helper to enumerate samples"""

    # We exclude directories since we store snapshots along with the samples.
    # This assumes that there are no tests in subdirectories of `SAMPLES_DIR`.
    assert SAMPLES_DIR.exists(), "directory with code samples not found"
    return sorted([sample for sample in SAMPLES_DIR.iterdir() if sample.is_file()])


# For each file in `SAMPLES_DIR` test formatting of the file.
@pytest.mark.parametrize(
    "sample", _get_samples(), ids=[str(os.path.basename(s)) for s in _get_samples()]
)
def test_samples(sample: Path, snapshot: SnapshotAssertion):
    input_ = zeekscript.Script(sample)

    assert input_.parse(), f"failed to parse input {sample}"
    assert not input_.has_error(), f"parse result for {sample} has parse errors"

    output = _format(input_)
    assert output == snapshot(), f"formatted {sample} inconsistent with snapshot"

    output2 = _format(input_)
    assert output2 == output, f"idempotency violation for {sample}"
