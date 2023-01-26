"""This module contains parser tooling for the zeekscript package."""
import os
import pathlib
import sys

try:
    # In order to use the tree-sitter parser we need to load the TS language .so
    # the TS Python bindings compiled at package build time (via our setup.py
    # tooling).  We use the following helpers when available (starting with
    # Python 3.9) to locate the it. With earlier Python versions we fall back to
    # using local path navigation and hope for the best.
    # https://importlib-resources.readthedocs.io/en/latest/using.html#file-system-or-zip-file
    from importlib.resources import files, as_file
except ImportError:

    def files(_):
        return pathlib.Path(os.path.dirname(os.path.realpath(__file__)))

    def as_file(source):
        return source


try:
    import tree_sitter
except ImportError:
    print("This package requires the tree_sitter package.")
    sys.exit(1)


class Parser:
    """tree_sitter.Parser abstraction that takes care of loading the TS Zeek language."""

    TS_PARSER = None  # A tree_sitter.Parser singleton

    def __init__(self):
        Parser.load_parser()

    def parse(self, text):
        """Returns a tree_sitter.Tree for the given script text.

        This tree may have errors, as indicated via its root node's has_error
        flag.
        """
        return Parser.TS_PARSER.parse(text)

    @classmethod
    def load_parser(cls):
        if cls.TS_PARSER is None:
            # Python voodoo to access the bindings library contained in this
            # package regardless of how we're loading the package. Details:
            # https://importlib-resources.readthedocs.io/en/latest/using.html#file-system-or-zip-file
            source = files(__package__).joinpath("zeek-language.so")
            with as_file(source) as lib:
                zeek_lang = tree_sitter.Language(str(lib), "zeek")
            cls.TS_PARSER = tree_sitter.Parser()
            cls.TS_PARSER.set_language(zeek_lang)
