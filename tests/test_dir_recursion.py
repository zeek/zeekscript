#! /usr/bin/env python
"""Tests related to recursive source processing."""
import argparse
import io
import os
import shutil
import sys
import unittest
import unittest.mock

from os.path import join

# Sets up sys.path and provides helpers
import testutils as tu

# This imports the tree-local zeekscript
from testutils import zeekscript


class TestRecursion(unittest.TestCase):
    def setUp(self):
        # Set up a small directory tree with Zeek scripts and other files
        shutil.rmtree("a", ignore_errors=True)
        os.makedirs(join("a", "b", "c"))

        shutil.copy(join(tu.DATA, "test1.zeek"), join("a", "test1.zeek"))
        shutil.copy(join(tu.DATA, "test1.zeek"), join("a", "test2.zeek"))
        shutil.copy(join(tu.DATA, "test1.zeek"), join("a", "b", "test3.txt"))
        shutil.copy(join(tu.DATA, "test1.zeek"), join("a", "b", "test4.zeek"))
        shutil.copy(join(tu.DATA, "test1.zeek"), join("a", "b", "c", "test5.zeek"))

    def tearDown(self):
        shutil.rmtree("a", ignore_errors=True)

    # pylint: disable-next=invalid-name
    def assertEqualContent(self, file1, file2):
        with open(file1, encoding="utf-8") as hdl1, open(
            file2, encoding="utf-8"
        ) as hdl2:
            self.assertEqual(hdl1.read(), hdl2.read())

    # pylint: disable-next=invalid-name
    def assertNotEqualContent(self, file1, file2):
        with open(file1, encoding="utf-8") as hdl1, open(
            file2, encoding="utf-8"
        ) as hdl2:
            self.assertNotEqual(hdl1.read(), hdl2.read())

    def test_recursive_formatting(self):
        parser = argparse.ArgumentParser()
        zeekscript.add_format_cmd(parser)
        args = parser.parse_args(["-i", "-r", "a"])

        # Python < 3.10 does not yet support parenthesized context managers:
        with unittest.mock.patch(
            "sys.stdout", new=io.StringIO()
        ) as out, unittest.mock.patch("sys.stderr", new=io.StringIO()):
            ret = args.run_cmd(args)
            self.assertEqual(ret, 0)
            self.assertEqual(out.getvalue(), "4 files processed, 0 errors\n")

        self.assertEqualContent(
            join(tu.DATA, "test1.zeek.out"), join("a", "test1.zeek")
        )
        self.assertEqualContent(
            join(tu.DATA, "test1.zeek.out"), join("a", "test2.zeek")
        )
        self.assertEqualContent(
            join(tu.DATA, "test1.zeek.out"), join("a", "b", "test4.zeek")
        )
        self.assertEqualContent(
            join(tu.DATA, "test1.zeek.out"), join("a", "b", "c", "test5.zeek")
        )

        self.assertNotEqualContent(
            join(tu.DATA, "test1.zeek.out"), join("a", "b", "test3.txt")
        )

    def test_recurse_inplace(self):
        parser = argparse.ArgumentParser()
        zeekscript.add_format_cmd(parser)
        args = parser.parse_args(["-ir"])

        with unittest.mock.patch("sys.stdout", new=io.StringIO()), unittest.mock.patch(
            "sys.stderr", new=io.StringIO()
        ) as err:
            ret = args.run_cmd(args)
            self.assertEqual(ret, 0)
            self.assertEqual(
                err.getvalue(),
                "warning: cannot use --inplace when reading from stdin, skipping it\n",
            )

    def test_dir_without_recurse(self):
        parser = argparse.ArgumentParser()
        zeekscript.add_format_cmd(parser)
        args = parser.parse_args(["-i", "a"])

        with unittest.mock.patch("sys.stdout", new=io.StringIO()), unittest.mock.patch(
            "sys.stderr", new=io.StringIO()
        ) as err:
            ret = args.run_cmd(args)
            self.assertEqual(ret, 0)
            self.assertEqual(
                err.getvalue(),
                'warning: "a" is a directory but --recursive not set, skipping it\n',
            )

    def test_recurse_without_inplace(self):
        parser = argparse.ArgumentParser()
        zeekscript.add_format_cmd(parser)
        args = parser.parse_args(["-r", "a"])

        with unittest.mock.patch("sys.stdout", new=io.StringIO()), unittest.mock.patch(
            "sys.stderr", new=io.StringIO()
        ) as err:
            ret = args.run_cmd(args)
            self.assertEqual(ret, 1)
            self.assertEqual(
                err.getvalue(), "error: recursive file processing requires --inplace\n"
            )


if __name__ == "__main__":
    sys.exit(not tu.test(__name__))
