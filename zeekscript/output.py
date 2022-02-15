import os
import sys

from .formatter import Formatter

class OutputStream:
    """A column-aware, trailing-whitespace-stripping wrapper for output streams."""
    def __init__(self, ostream):
        """OutputStream constructor. The ostream argument is a file-like object."""
        self._ostream = ostream
        self._col = 0 # 0-based column the next character goes into.
        self._space_indent = 0

    def set_space_indent(self, num):
        self._space_indent = num

    def write(self, data):
        for chunk in data.splitlines(keepends=True):
            if chunk.endswith(Formatter.NL):
                # Remove any trailing whitespace
                chunk = chunk.rstrip() + Formatter.NL

            try:
                if self._ostream == sys.stdout:
                    # Clunky: must write string here, not bytes. We could
                    # use _ostream.buffer -- not sure how portable that is.
                    self._ostream.write(chunk.decode('UTF-8'))
                else:
                    self._ostream.write(chunk)
            except BrokenPipeError:
                #  https://docs.python.org/3/library/signal.html#note-on-sigpipe:
                devnull = os.open(os.devnull, os.O_WRONLY)
                os.dup2(devnull, sys.stdout.fileno())
                sys.exit(1)

            self._col += len(chunk)
            if chunk.endswith(Formatter.NL):
                self._col = 0

    def write_space_indent(self):
        if self._space_indent > 0:
            self.write(b' ' * 4 * self._space_indent)

    def get_column(self):
        return self._col


def print_error(*args, **kwargs):
    """A print() wrapper that writes to stderr."""
    print(*args, file=sys.stderr, **kwargs)
