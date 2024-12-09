"""Helpers for the various test_*.py files."""

import os

import zeekscript


def fix_lineseps(content):
    """Standardizes newlines in the given content to those of the local platform."""
    if isinstance(content, bytes):
        out = content.replace(b"\r\n", b"\n")
        out = out.replace(b"\n", zeekscript.Formatter.NL)
    else:
        out = content.replace("\r\n", "\n")
        out = out.replace("\n", os.linesep)

    return out


def normalize(content):
    """Encodes the given content string to UTF-8 if not already binary, and
    standardizes newlines.
    """
    if not isinstance(content, bytes):
        out = content.encode("UTF-8")
    else:
        out = content

    return fix_lineseps(out)


# A small unformatted source sample for general testing.
SAMPLE_UNFORMATTED = """\
global    foo=1   +2    ;
"""


# A small formatted source sample for general testing.
SAMPLE_FORMATTED = """\
global foo = 1 + 2;
"""
