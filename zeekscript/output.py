"""Functionality related to output."""

import os
import sys
from types import TracebackType
from typing import Any, BinaryIO, TextIO

from .formatter import Formatter, Hint


class Output:
    """A chunk of data to write out.

    The OutputStream class uses this for buffering up data chunks that make up a
    formatted line, deciding when/whether to intersperse additional line breaks.
    """

    def __init__(
        self, data: bytes, formatter: Formatter, align_column: int = 0, hints: "Hint | None" = None
    ) -> None:
        self.data = data
        self.formatter = formatter
        self.align_column = align_column  # Column to align to if linebreak before this
        self.hints = hints  # Hints active when this output was written


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

    def __init__(
        self, ostream: BinaryIO | TextIO, enable_linebreaks: bool = True
    ) -> None:
        """OutputStream constructor. The ostream argument is a file-like object."""
        self._ostream = ostream
        self._col = 0  # 0-based column the next character goes into.
        self._tab_indent = 0  # Number of tabs indented in current line

        # Series of Output objects that makes up a formatted but un-wrapped line.
        self._linebuffer: list[Output] = []

        # Series of byte sequences actually written out, post line-wrap. This is
        # line-buffered, to allow removal of trailing whitespace. Details in
        # self._write().
        self._writebuffer: list[bytes] = []

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

        # When set to a positive column number, linebreaks will align to this
        # column (using spaces after tabs) instead of the default SPACE_INDENT.
        # Used for aligning function arguments to the opening parenthesis.
        self._align_column: int = 0

        # Current hints to apply to output items (for tracking wrap behavior)
        self._current_hints: Hint | None = None

    def __enter__(self) -> "OutputStream":
        return self

    def __exit__(
        self,
        _exc_type: type[BaseException] | None,
        _exc_value: BaseException | None,
        _exc_traceback: TracebackType | None,
    ) -> None:
        self._flush_line()
        self._flush_writes()

    def use_linebreaks(self, enable: bool) -> None:
        self._use_linebreaks = enable

    def use_tab_indent(self, enable: bool) -> None:
        self._use_tab_indent = enable

    def use_space_align(self, enable: bool) -> None:
        self._use_space_align = enable

    def set_align_column(self, column: int) -> None:
        """Set the column to align to on linebreaks (0 to use default)."""
        self._align_column = column

    def set_hints(self, hints: Hint | None) -> None:
        """Set hints to apply to subsequent output items."""
        self._current_hints = hints

    def write(self, data: bytes, formatter: Formatter, raw: bool = False) -> None:
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
            self._linebuffer.append(
                Output(chunk, formatter, self._align_column, self._current_hints)
            )
            if chunk.endswith(Formatter.NL):
                self._flush_line()

    def write_tab_indent(self, formatter: Formatter) -> None:
        if self._use_tab_indent:
            self._tab_indent = formatter.indent
            self.write(b"\t" * self._tab_indent, formatter)

    def write_space_align(self, formatter: Formatter) -> None:
        if self._use_space_align:
            self.write(b" " * 4, formatter)

    def get_column(self) -> int:
        return self._col

    def get_visual_column(self) -> int:
        """Get the visual column position, accounting for tab width."""
        # Count visual columns by scanning the current line buffer
        visual_col = 0
        for out in self._linebuffer:
            for byte in out.data:
                if byte == ord(b"\t"):
                    # Round up to next tab stop
                    visual_col = ((visual_col // self.TAB_SIZE) + 1) * self.TAB_SIZE
                else:
                    visual_col += 1
        return visual_col

    def _flush_line(self) -> None:
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

        col_flushed = 0  # Visual column up to which we've currently written a line
        tbd: list[Output] = []  # Outputs to be done
        tbd_len = 0  # Visual length of the to-be-done output
        line_items = 0  # Number of items (tokens, not whitespace) on formatted line
        using_break_hints = False  # Whether we've used advisory linebreak hints yet

        def visual_width(data: bytes, start_col: int) -> int:
            """Calculate visual width of data, accounting for tab stops."""
            col = start_col
            for byte in data:
                if byte == ord(b"\t"):
                    col = ((col // self.TAB_SIZE) + 1) * self.TAB_SIZE
                else:
                    col += 1
            return col - start_col

        def flush_tbd() -> None:
            nonlocal tbd, tbd_len, col_flushed
            for tbd_out in tbd:
                self._write(tbd_out.data)
                col_flushed += visual_width(tbd_out.data, col_flushed)
            tbd = []
            tbd_len = 0

        # break_align_col will be set after chosen_break_idx is determined
        break_align_col = 0

        def write_linebreak() -> None:
            nonlocal tbd, tbd_len, col_flushed
            self._write(Formatter.NL)
            self._write(b"\t" * self._tab_indent)
            tab_col = self._tab_indent * self.TAB_SIZE

            # Find alignment and hints from the first non-whitespace item
            # that will follow the linebreak
            align_col = break_align_col
            use_tab_wrap = False
            for item in tbd:
                if item.data.strip():
                    if item.hints is not None and Hint.INIT_ELEMENT in item.hints:
                        use_tab_wrap = True
                    break

            if use_tab_wrap:
                # Initializer elements use an extra tab for continuation
                self._write(b"\t")
                col_flushed = tab_col + self.TAB_SIZE
            elif align_col > 0:
                # Align to the specified column (e.g., after opening paren)
                space_count = max(0, align_col - tab_col)
                self._write(b" " * space_count)
                col_flushed = tab_col + space_count
            else:
                self._write(b" " * self.SPACE_INDENT)
                col_flushed = tab_col + self.SPACE_INDENT

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

        # First pass: calculate total line length and find valid break points
        # A break point is valid if it's after a non-whitespace token without NO_LB_AFTER
        # Track nesting depth to prefer breaks at the outermost level
        # Also track whether the break is after a comma (preferred for argument lists)
        total_len = 0
        all_break_points: list[tuple[int, int, int, bool]] = []  # (index, visual_column, nesting_depth, after_comma)
        has_good_after_lb = -1  # Index of token with GOOD_AFTER_LB hint
        has_init_lenient = False
        nesting_depth = 0

        for i, out in enumerate(self._linebuffer):
            token = out.data.strip()
            # Track nesting depth for parentheses, brackets, braces
            if token in (b"(", b"[", b"{"):
                nesting_depth += 1
            elif token in (b")", b"]", b"}"):
                nesting_depth -= 1

            if Hint.ZERO_WIDTH not in out.formatter.hints:
                total_len += visual_width(out.data, total_len)

            if token:
                if out.hints is not None and Hint.INIT_LENIENT in out.hints:
                    has_init_lenient = True
                if Hint.GOOD_AFTER_LB in out.formatter.hints:
                    has_good_after_lb = i
                if Hint.NO_LB_AFTER not in out.formatter.hints:
                    is_comma = token == b","
                    all_break_points.append((i, total_len, nesting_depth, is_comma))

        # Only use break points at the appropriate nesting depth
        # For line wrapping, we want to break INSIDE function calls (depth 1+), not outside (depth 0)
        # Among depths 1+, prefer the minimum depth (avoid breaking inside nested calls)
        # Also prefer breaks after commas (for argument lists) over breaks at operators
        break_points: list[tuple[int, int]] = []
        if all_break_points:
            # Separate depth 0 from depth 1+
            depths = sorted(set(bp[2] for bp in all_break_points))
            non_zero_depths = [d for d in depths if d > 0]

            # Prefer minimum non-zero depth (inside outermost call, not nested)
            # Fall back to depth 0 only if no non-zero depths available
            target_depths = non_zero_depths if non_zero_depths else depths

            for depth in target_depths:
                at_depth = [bp for bp in all_break_points if bp[2] == depth]
                if at_depth:
                    # At this depth, prefer breaks after commas if available
                    comma_breaks = [(bp[0], bp[1]) for bp in at_depth if bp[3]]
                    if comma_breaks:
                        break_points = comma_breaks
                    else:
                        break_points = [(bp[0], bp[1]) for bp in at_depth]
                    break

        effective_max_len = self.MAX_LINE_LEN * 2 if has_init_lenient else self.MAX_LINE_LEN

        # Determine if we need to break and where
        chosen_break_idx = -1

        if total_len > effective_max_len and line_items >= self.MIN_LINE_ITEMS:
            # Check if GOOD_AFTER_LB hint should take precedence
            if has_good_after_lb >= 0 and self._col > self.MAX_LINE_LEN:
                chosen_break_idx = has_good_after_lb
                using_break_hints = True
            elif break_points:
                # Calculate continuation indent for balanced break calculation
                tab_col = self._tab_indent * self.TAB_SIZE
                # Get alignment from first potential break point's following content
                align_col = 0
                if break_points:
                    first_break_idx = break_points[0][0]
                    for j in range(first_break_idx + 1, len(self._linebuffer)):
                        if self._linebuffer[j].data.strip():
                            align_col = self._linebuffer[j].align_column
                            break
                if align_col > 0:
                    continuation_indent = align_col
                else:
                    continuation_indent = tab_col + self.SPACE_INDENT

                # Find break point that balances the two lines while keeping both under limit.
                # Strategy: find the latest break point where both lines fit and
                # line2 has meaningful content (at least half of max line length).

                # Find all break points where BOTH lines would fit
                valid_breaks: list[tuple[int, int, int]] = []  # (index, col, line2_len)
                for idx, col in break_points:
                    remainder = total_len - col
                    line2_len = continuation_indent + remainder
                    # Both lines must fit under MAX_LINE_LEN
                    if col <= self.MAX_LINE_LEN and line2_len <= self.MAX_LINE_LEN:
                        valid_breaks.append((idx, col, line2_len))

                # If no valid breaks, pick the break that keeps line1 closest to 80
                # Prefer breaks where line1 fits (col <= 80), then pick closest to limit
                if not valid_breaks:
                    if break_points:
                        target = self.MAX_LINE_LEN
                        # Score: (1 if over limit else 0, distance from limit)
                        def score(bp: tuple[int, int]) -> tuple[int, int]:
                            col = bp[1]
                            if col <= target:
                                return (0, target - col)  # Under: prefer closer
                            else:
                                return (1, col - target)  # Over: prefer less excess
                        best_bp = min(break_points, key=score)
                        best_break = best_bp[0]
                    else:
                        best_break = -1
                else:
                    # Among valid breaks, prefer the latest one where line2 is
                    # at least 2/3 the length of line1. This keeps the lines
                    # reasonably balanced while maximizing content on line1.
                    best_break = -1
                    for idx, col, line2_len in reversed(valid_breaks):
                        if line2_len >= col * 2 // 3:
                            best_break = idx
                            break
                    # If no break satisfies the criterion, use the most balanced one
                    if best_break < 0 and valid_breaks:
                        best_break = min(valid_breaks, key=lambda x: abs(x[1] - x[2]))[0]

                if best_break >= 0:
                    # Check if break is "worth it" (enough excess)
                    break_col = next(col for idx, col in break_points if idx == best_break)
                    remainder = total_len - break_col
                    if remainder >= self.MIN_LINE_EXCESS or total_len > self.MAX_LINE_LEN + self.MIN_LINE_EXCESS:
                        chosen_break_idx = best_break

        # Get alignment for the line break from the first non-whitespace token after the break
        # If there's no non-whitespace content after the break, don't break (it's pointless)
        if chosen_break_idx >= 0:
            has_content_after = False
            for j in range(chosen_break_idx + 1, len(self._linebuffer)):
                if self._linebuffer[j].data.strip():
                    break_align_col = self._linebuffer[j].align_column
                    has_content_after = True
                    break
            if not has_content_after:
                chosen_break_idx = -1  # Cancel the break - nothing meaningful would go on line2

        # Second pass: process the line with the chosen break point
        break_done = False
        for i, out in enumerate(self._linebuffer):
            tbd.append(out)

            if Hint.ZERO_WIDTH not in out.formatter.hints:
                tbd_len += visual_width(out.data, col_flushed + tbd_len)

            # After passing the break point, flush line1 and write linebreak.
            # We do this AFTER appending the current token so that write_linebreak()
            # can look at tbd to find alignment info from line2's first token.
            if i > chosen_break_idx and chosen_break_idx >= 0 and not break_done:
                # tbd contains line1 content (indices 0..chosen_break_idx) plus
                # at least one token from line2. Split them.
                line1_end = chosen_break_idx + 1
                line1_items = []
                line2_items = []
                for j, item in enumerate(tbd):
                    # Find position in original linebuffer
                    orig_idx = i - len(tbd) + 1 + j
                    if orig_idx <= chosen_break_idx:
                        line1_items.append(item)
                    else:
                        line2_items.append(item)

                # Flush line1
                tbd = line1_items
                flush_tbd()

                # Write linebreak with line2 items in tbd for alignment lookup
                tbd = line2_items
                write_linebreak()
                break_done = True

        # Final flush
        flush_tbd()

        self._linebuffer = []
        self._col = 0
        self._align_column = 0  # Clear alignment after line is processed

    def _flush_writes(self) -> None:
        if self._writebuffer and not self._writebuffer[-1].endswith(Formatter.NL):
            self._write(Formatter.NL)

    def _write(self, data: bytes) -> None:
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
            if isinstance(self._ostream, TextIO):
                # Clunky: must write string here, not bytes. We could
                # use _ostream.buffer -- not sure how portable that is.
                # self._ostream.write(output.decode("UTF-8"))
                self._ostream.write(output.decode("UTF-8"))
            else:
                self._ostream.write(output)
        except BrokenPipeError:
            #  https://docs.python.org/3/library/signal.html#note-on-sigpipe:
            devnull = os.open(os.devnull, os.O_WRONLY)
            os.dup2(devnull, sys.stdout.fileno())
            sys.exit(1)


def print_error(*args: object, **kwargs: Any) -> None:
    """A print() wrapper that writes to stderr."""
    print(*args, file=sys.stderr, **kwargs)
