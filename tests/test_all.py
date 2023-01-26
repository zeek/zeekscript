#! /usr/bin/env python
"""Helper to run all available tests."""
import sys

import test_formatting
import test_dir_recursion
import test_pylint

if __name__ == "__main__":
    # Each test() call returns True if successful, so only exit with 0 when they
    # all succeed.
    sys.exit(
        not all(
            (
                test_formatting.test(),
                test_dir_recursion.test(),
                test_pylint.test(),
            )
        )
    )
