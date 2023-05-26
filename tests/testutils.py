"""Helpers for the various test_*.py files."""
import os
import sys

TESTS = os.path.dirname(os.path.realpath(__file__))
ROOT = os.path.normpath(os.path.join(TESTS, ".."))
DATA = os.path.normpath(os.path.join(TESTS, "data"))

# Prepend the tree's root folder to the module searchpath so we find zeekscript
# via it. This allows tests to run without package installation. (We do need a
# package build though, so the .so bindings library gets created.)
sys.path.insert(0, ROOT)

import zeekscript  # pylint: disable=wrong-import-position


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
