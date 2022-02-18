import os
import sys

from .formatter import Formatter, Hint

class Output:
    """A chunk of data to write out.

    The OutputStream class uses this for buffering up data chunks that make up a
    formatted line, deciding when/whether to intersperse additional line breaks.
    """
    def __init__(self, data, formatter):
        self.data = data
        self.formatter = formatter


class OutputStream:
    """An indenting, column-aware, line-buffered, trailing-whitespace-stripping
    output stream wrapper.
    """
    MAX_LINE_LEN = 80
    SPACE_INDENT = 4

    def __init__(self, ostream):
        """OutputStream constructor. The ostream argument is a file-like object."""
        self._ostream = ostream
        self._col = 0 # 0-based column the next character goes into.
        self._tab_indent = 0 # Number of tabs indented in current line
        self._space_indent = 0 # Number of alignment spaces we add to that
        self._linebuffer = [] # Sequence of Output objects that makes up a line

    def set_space_indent(self, num):
        self._space_indent = num

    def write(self, data, formatter):
        for chunk in data.splitlines(keepends=True):
            if chunk.endswith(Formatter.NL):
                # Remove any trailing whitespace
                chunk = chunk.rstrip() + Formatter.NL

            self._linebuffer.append(Output(chunk, formatter))
            self._col += len(chunk)

            if chunk.endswith(Formatter.NL):
                self._flush_line()

    def write_tab_indent(self, formatter):
        self._tab_indent = formatter.indent
        self.write(b'\t' * self._tab_indent, formatter)

    def write_space_indent(self, formatter):
        if self._space_indent > 0:
            self.write(b' ' * 4 * self._space_indent, formatter)

    def get_column(self):
        return self._col

    def _flush_line(self):
        """Helper that flushes out the built-up line buffer.

        This iterates over the Output objects in self._linebuffer, deciding
        whether to write them out right away or in batches, possibly after
        newlines, depending on line-breaking hints present in the formatter
        objects linked from the Output instances.
        """
        col_flushed = 0 # Column up to which we've currently written a line
        tbd = [] # Outputs to be done
        tbd_len = 0 # Length of the to-be-done output (in characters)
        using_break_hints = False # Whether we've used advisory linebreak hints yet

        def flush_tbd():
            nonlocal tbd, tbd_len, col_flushed
            for tbd_out in tbd:
                self._write(tbd_out.data)
                col_flushed += len(tbd_out.data)
            tbd = []
            tbd_len = 0

        def write_linebreak():
            nonlocal tbd, tbd_len, col_flushed
            self._write(Formatter.NL)
            self._write(b'\t' * self._tab_indent)
            self._write(b' ' * self.SPACE_INDENT)
            col_flushed = self._tab_indent + self.SPACE_INDENT

            # Remove any pure whitespace from the beginning of the
            # continuation of the line we just broke:
            while tbd and not tbd[0].data.strip():
                tbd_len -= len(tbd.pop(0).data)

        for out, out_next in zip(self._linebuffer, self._linebuffer[1:] + [None]):
            tbd.append(out)

            # Ignore this output's width if requested
            if not Hint.ZERO_WIDTH & out.formatter.hints:
                tbd_len += len(out.data)

            # Never write out mid-line whitespace right away: if there's data
            # following it that exceeds max line length, we'd produce trailing
            # whitespace. We may instead push whitespace onto the beginning of
            # the next line, where we suppress it (see below).
            if not out.data.strip():
                continue

            # Honor hints that suppress linebreaks between this and the next
            # output chunk.
            if Hint.NO_LB_AFTER & out.formatter.hints:
                continue
            if out_next is not None and Hint.NO_LB_BEFORE & out_next.formatter.hints:
                continue

            # If the full line is too long and this chunk says it's good after a
            # linebreak, then break now. This is what helps align boolean
            # operators.
            if Hint.GOOD_AFTER_LB & out.formatter.hints and self._col > self.MAX_LINE_LEN:
                write_linebreak()
                using_break_hints = True

            # If we naturally exceed line length while flushing a line, break
            # it, possibly repeatedly. But if we've ever used the GOOD_AFTER_LB
            # hint, rely exclusively on it for breaks, because the resulting mix
            # tends to look messy otherwise.
            elif col_flushed + tbd_len > self.MAX_LINE_LEN and not using_break_hints:
                write_linebreak()

            flush_tbd()

        # Another flush to finish any leftovers
        flush_tbd()

        self._linebuffer = []
        self._col = 0

    def _write(self, data):
        try:
            if self._ostream == sys.stdout:
                # Clunky: must write string here, not bytes. We could
                # use _ostream.buffer -- not sure how portable that is.
                self._ostream.write(data.decode('UTF-8'))
            else:
                self._ostream.write(data)
        except BrokenPipeError:
            #  https://docs.python.org/3/library/signal.html#note-on-sigpipe:
            devnull = os.open(os.devnull, os.O_WRONLY)
            os.dup2(devnull, sys.stdout.fileno())
            sys.exit(1)


def print_error(*args, **kwargs):
    """A print() wrapper that writes to stderr."""
    print(*args, file=sys.stderr, **kwargs)
