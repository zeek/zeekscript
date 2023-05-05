#! /usr/bin/env python
"""Helper to run all available tests."""
import sys

# pylint: disable=unused-import
import test_dir_recursion
import test_formatting
import test_script

import testutils as tu

if __name__ == "__main__":
    # Each test() returns True if successful, so only exit with 0 when all succeed.
    modules = [mod for mod in sys.modules if mod.startswith("test_")]
    sys.exit(not all(tu.test(mod) for mod in modules))
