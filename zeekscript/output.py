"""Functionality related to output."""

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
    """An indenting, column-aware, line-buffered, line-wrapping,
    trailing-whitespace-stripping output stream wrapper. When used as a context
    manager, it also ensures that exiting context always finishes output with a
    newline.
    """

    MAX_LINE_LEN = 80  # Column at which we consider wrapping.
    MIN_LINE_ITEMS = 5  # Required items on a line to consider wrapping.
    MIN_LINE_EXCESS = 5  # Minimum characters that a line needs to be too long.
    TAB_SIZE = 8  # How many visible characters we chalk up for a tab.
    SPACE_INDENT = 4  # When wrapping, add this many spaces onto tab-indentation.

    def __init__(self, ostream, enable_linebreaks=True):
        """OutputStream constructor. The ostream argument is a file-like object."""
        self._ostream = ostream
        self._col = 0  # 0-based column the next character goes into.
        self._tab_indent = 0  # Number of tabs indented in current line

        # Series of Output objects that makes up a formatted but un-wrapped line.
        self._linebuffer = []

        # Series of byte sequences actually written out, post line-wrap. This is
        # line-buffered, to allow removal of trailing whitespace. Details in
        # self._write().
        self._writebuffer = []

        # Whether we'll consider linebreaks at all. When False, long lines will
        # never wrap. When True, linebreaks will generally happen, but
        # formatters may pause them temporarily via the _use_linebreaks flag
        # below.
        self._enable_linebreaks = enable_linebreaks

        self._use_linebreaks = True  # Whether line-breaking is in effect.
        self._use_tab_indent = True  # Whether tab-indentation is in effect.

        # Whether to tuck on space-alignments independently of our own linebreak
        # logic. (Some formatters request this.) These alignments don't
        # currently align properly to a particular character in the previous
        # line; they just add a few spaces.
        self._use_space_align = False

    def __enter__(self):
        return self

    def __exit__(self, _exc_type, _exc_value, _exc_traceback):
        self._flush_line()
        self._flush_writes()

    def use_linebreaks(self, enable):
        self._use_linebreaks = enable

    def use_tab_indent(self, enable):
        self._use_tab_indent = enable

    def use_space_align(self, enable):
        self._use_space_align = enable

    def write(self, data, formatter, raw=False):
        if raw:
            self._flush_line()
            self._write(data)
            # Sync column count as per last line content in raw data:
            self._col = len(data.split(Formatter.NL)[-1])
            return

        # For troubleshooting received hinting
        # print_error('XXX "%s" %s' % (data, formatter.hints))

        # In case the data spans multiple lines, break up the lines now.
        # Potential line-wrapping is applied when we flush individual lines.
        for chunk in data.splitlines(keepends=True):
            self._col += len(chunk)
            self._linebuffer.append(Output(chunk, formatter))
            if chunk.endswith(Formatter.NL):
                self._flush_line()

    def write_tab_indent(self, formatter):
        if self._use_tab_indent:
            self._tab_indent = formatter.indent
            self.write(b"\t" * self._tab_indent, formatter)

    def write_space_align(self, formatter):
        if self._use_space_align:
            self.write(b" " * 4, formatter)

    def get_column(self):
        return self._col

    def _flush_line(self):
        """Flushes out the line buffer, stripping trailing whitespace.

        Without linewrapping, this simply flushes the line buffer. With
        linewrapping this iterates over the line buffer, deciding whether to
        write out data chunks right away or in to-be-done batches, possibly
        after newlines, depending on line-breaking hints present in the
        formatter objects linked from the Output instances.
        """

        # Without linebreaking active, just flush the buffer.
        if not self._enable_linebreaks or not self._use_linebreaks:
            for out in self._linebuffer:
                self._write(out.data)

            self._linebuffer = []
            self._col = 0
            return

        col_flushed = 0  # Column up to which we've currently written a line
        tbd = []  # Outputs to be done
        tbd_len = 0  # Length of the to-be-done output (in characters)
        line_items = 0  # Number of items (tokens, not whitespace) on formatted line
        using_break_hints = False  # Whether we've used advisory linebreak hints yet

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
            self._write(b"\t" * self._tab_indent)
            self._write(b" " * self.SPACE_INDENT)
            col_flushed = self._tab_indent * self.TAB_SIZE + self.SPACE_INDENT

            # Remove any pure whitespace from the beginning of the
            # continuation of the line we just broke:
            while tbd and not tbd[0].data.strip():
                tbd_len -= len(tbd.pop(0).data)

        # Count number of non-whitespace items on the line. This helps with some
        # linebreak heuristics below.
        for out in self._linebuffer:
            if out.data.strip():
                line_items += 1

        # It is logistically more difficult to honor NO_LB_BEFORE as it arises,
        # because we need to "look-ahead" to prevent breaking. To simplify,
        # reverse-iterate over the line's tokens and tuck NO_LB_AFTER onto
        # tokens that precede NO_LB_BEFORE.
        needs_no_lb_after = False
        for out in self._linebuffer[::-1]:
            if out.data.strip() and needs_no_lb_after:
                out.formatter.hints |= Hint.NO_LB_AFTER
                needs_no_lb_after = False
            if Hint.NO_LB_BEFORE in out.formatter.hints:
                needs_no_lb_after = True

        # Now do the actual line processing.
        for out in self._linebuffer:
            tbd.append(out)

            # Establish how long the pending chunk is, given hinting:
            if Hint.ZERO_WIDTH not in out.formatter.hints:
                tbd_len += len(out.data)

            # Don't make line-wrapping decisions based on whitespace:
            if not out.data.strip():
                continue

            # We name the various conditions going into the linebreak decision,
            # so we can report them for troubleshooting and act on them below

            # If the line is too long and this chunk says it best follows a
            # break, then break now. This helps align e.g. multi-part boolean
            # conditionals. This needs to take precedence over NO_LB_AFTER,
            # see next condition.
            cnd_good_after_lb = (
                Hint.GOOD_AFTER_LB in out.formatter.hints
                and self._col > self.MAX_LINE_LEN
            )

            # If the caller requested no line break, abide.
            cnd_no_lb_after = Hint.NO_LB_AFTER in out.formatter.hints

            # Similarly, if we git GOOD_AFTER_LB earlier, abide.
            cnd_no_break_hints = not using_break_hints

            # We need to exceed the MAX_LINE_LEN limit with what's pending.
            cnd_line_too_long = col_flushed + tbd_len > self.MAX_LINE_LEN

            # The pending length must be "worth it". That is, don't break if the
            # TBD len is just a little bit over. But do so if we're just too
            # long overall now.
            cnd_enough_excess = (
                tbd_len >= self.MIN_LINE_EXCESS
                or col_flushed > self.MAX_LINE_LEN + self.MIN_LINE_EXCESS
            )

            # If there are only very few items on the line to begin with, don't
            # bother: breaking these also looks messy. This often covers the
            # case of a line consisting mostly of a long string.
            cnd_enough_line_items = line_items >= self.MIN_LINE_ITEMS

            # If the TBD items would immediately exceed the line limit again
            # after wrapping, don't bother. This covers the case of e.g. very
            # long strings looking silly when alone on a new line. (This doesn't
            # interfere with bit-by-bit repeated linebreaks of a very long line
            # -- that still happens when we build up the next TBD batch.)
            cnd_no_addl_wrap = (
                self._tab_indent * self.TAB_SIZE + tbd_len < self.MAX_LINE_LEN
            )

            # Helpful for tracing linebreak decision-making:
            # print_error('XXX gal:%d nla:%d nbh:%d tl:%d ex:%d ei:%d naw:%d | %s %s %s' % (
            #    cnd_good_after_lb, cnd_no_lb_after, cnd_no_break_hints,
            #    cnd_line_too_long, cnd_enough_excess, cnd_enough_line_items,
            #    cnd_no_addl_wrap, out.data, col_flushed, tbd_len))

            # If the line is too long and this chunk says it best follows a
            # break, then break now. This helps align e.g. multi-part boolean
            # conditionals. This needs to take precedence over NO_LB_AFTER.
            if cnd_good_after_lb:
                write_linebreak()
                using_break_hints = True

            # Honor hinted linebreak suppression around this chunk.
            elif cnd_no_lb_after:
                continue

            # Finally actually linebreak as needed:
            elif (
                cnd_no_break_hints
                and cnd_line_too_long
                and cnd_enough_excess
                and cnd_enough_line_items
                and cnd_no_addl_wrap
            ):
                write_linebreak()

            flush_tbd()

        # Another flush to finish any leftovers
        flush_tbd()

        self._linebuffer = []
        self._col = 0

    def _flush_writes(self):
        if self._writebuffer and not self._writebuffer[-1].endswith(Formatter.NL):
            self._write(Formatter.NL)

    def _write(self, data):
        self._writebuffer.append(data)

        # Data can only have newlines at the end, since we already split any
        # mid-data newlines earlier. So check for trailing newlines, and if
        # found, strip trailing whitespace and flush.
        if not data.endswith(Formatter.NL):
            return

        output = b"".join(self._writebuffer)
        output = output.rstrip() + Formatter.NL
        self._writebuffer = []

        try:
            if self._ostream == sys.stdout:
                # Clunky: must write string here, not bytes. We could
                # use _ostream.buffer -- not sure how portable that is.
                self._ostream.write(output.decode("UTF-8"))
            else:
                self._ostream.write(output)
        except BrokenPipeError:
            #  https://docs.python.org/3/library/signal.html#note-on-sigpipe:
            devnull = os.open(os.devnull, os.O_WRONLY)
            os.dup2(devnull, sys.stdout.fileno())
            sys.exit(1)


def print_error(*args, **kwargs):
    """A print() wrapper that writes to stderr."""
    print(*args, file=sys.stderr, **kwargs)
