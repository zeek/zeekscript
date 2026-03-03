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

    def get_align_column(self) -> int:
        """Get the current alignment column."""
        return self._align_column

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

        def find_break_points(
            items: list[Output], start_col: int, start_depth: int = 0
        ) -> tuple[list[tuple[int, int, int, bool]], int, bool, int, int]:
            """Find valid break points in a list of items.

            Returns (all_break_points, total_len, has_init_lenient, has_good_after_lb, end_depth)
            where all_break_points is list of (index, visual_column, nesting_depth, is_comma)
            """
            total_len = start_col
            all_break_points: list[tuple[int, int, int, bool]] = []
            has_good_after_lb = -1
            has_init_lenient = False
            nesting_depth = start_depth

            for i, out in enumerate(items):
                token = out.data.strip()
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

            return all_break_points, total_len, has_init_lenient, has_good_after_lb, nesting_depth

        def filter_break_points(
            all_break_points: list[tuple[int, int, int, bool]]
        ) -> list[tuple[int, int]]:
            """Filter break points by depth and comma preference."""
            if not all_break_points:
                return []

            depths = sorted(set(bp[2] for bp in all_break_points))
            non_zero_depths = [d for d in depths if d > 0]
            target_depths = non_zero_depths if non_zero_depths else depths

            for depth in target_depths:
                at_depth = [bp for bp in all_break_points if bp[2] == depth]
                if at_depth:
                    comma_breaks = [(bp[0], bp[1]) for bp in at_depth if bp[3]]
                    if comma_breaks:
                        return comma_breaks
                    else:
                        return [(bp[0], bp[1]) for bp in at_depth]
            return []

        def choose_break(
            items: list[Output],
            break_points: list[tuple[int, int]],
            total_len: int,
            start_col: int,
            continuation_indent: int,
            effective_max_len: int,
        ) -> int:
            """Choose the best break point index, or -1 if no break needed."""
            if total_len <= effective_max_len:
                return -1
            if len([o for o in items if o.data.strip()]) < self.MIN_LINE_ITEMS:
                return -1
            if not break_points:
                return -1

            # Adjust break point columns relative to start_col
            adjusted_breaks = [(idx, col - start_col) for idx, col in break_points]

            # Find breaks where line1 fits under limit
            # Note: col here is the line length up to and including that token
            valid_breaks: list[tuple[int, int, int]] = []
            for idx, col in adjusted_breaks:
                abs_col = start_col + col
                remainder = total_len - abs_col
                line2_len = continuation_indent + remainder
                if abs_col <= self.MAX_LINE_LEN and line2_len <= self.MAX_LINE_LEN:
                    valid_breaks.append((idx, abs_col, line2_len))

            if not valid_breaks:
                # Pick break that keeps line1 closest to 80
                target = self.MAX_LINE_LEN

                def score(bp: tuple[int, int]) -> tuple[int, int]:
                    abs_col = start_col + bp[1]
                    if abs_col <= target:
                        return (0, target - abs_col)
                    else:
                        return (1, abs_col - target)

                best_bp = min(adjusted_breaks, key=score)
                best_break = best_bp[0]
            else:
                # Prefer latest break where line2 >= 2/3 of line1
                best_break = -1
                for idx, abs_col, line2_len in reversed(valid_breaks):
                    if line2_len >= abs_col * 2 // 3:
                        best_break = idx
                        break
                if best_break < 0:
                    best_break = min(valid_breaks, key=lambda x: abs(x[1] - x[2]))[0]

            # Check if break is worth it
            if best_break >= 0:
                break_col = next(
                    start_col + col for idx, col in adjusted_breaks if idx == best_break
                )
                remainder = total_len - break_col
                if remainder < self.MIN_LINE_EXCESS and total_len <= self.MAX_LINE_LEN + self.MIN_LINE_EXCESS:
                    return -1

            # Check there's content after the break
            if best_break >= 0:
                has_content = any(items[j].data.strip() for j in range(best_break + 1, len(items)))
                if not has_content:
                    return -1

            return best_break

        # Process items iteratively, breaking as many times as needed
        items_remaining = list(self._linebuffer)
        current_depth = 0  # Track nesting depth across iterations

        while items_remaining:
            # Find break points for remaining items
            all_bps, total_len, has_init_lenient, _, end_depth = find_break_points(
                items_remaining, col_flushed, current_depth
            )
            break_points = filter_break_points(all_bps)

            effective_max_len = self.MAX_LINE_LEN * 2 if has_init_lenient else self.MAX_LINE_LEN

            # Determine continuation indent for potential breaks
            tab_col = self._tab_indent * self.TAB_SIZE
            cont_indent = 0
            if break_points:
                first_break_idx = break_points[0][0]
                for j in range(first_break_idx + 1, len(items_remaining)):
                    if items_remaining[j].data.strip():
                        cont_indent = items_remaining[j].align_column
                        break
            if cont_indent == 0:
                cont_indent = tab_col + self.SPACE_INDENT

            chosen_break = choose_break(
                items_remaining, break_points, total_len, col_flushed, cont_indent, effective_max_len
            )

            if chosen_break >= 0:
                # Get the nesting depth at the break point for the next iteration
                depth_at_break = current_depth
                for bp in all_bps:
                    if bp[0] == chosen_break:
                        depth_at_break = bp[2]
                        break

                # Output items up to and including break point
                for out in items_remaining[: chosen_break + 1]:
                    self._write(out.data)
                    col_flushed += visual_width(out.data, col_flushed)

                # Update depth for next iteration
                current_depth = depth_at_break

                # Prepare remaining items and get alignment
                items_remaining = items_remaining[chosen_break + 1 :]

                # Remove leading whitespace from remaining
                while items_remaining and not items_remaining[0].data.strip():
                    items_remaining.pop(0)

                # Find alignment column from first remaining item
                break_align_col = 0
                if items_remaining:
                    break_align_col = items_remaining[0].align_column

                # Write linebreak
                tbd = items_remaining  # For write_linebreak to inspect
                write_linebreak()
            else:
                # No break needed - output all remaining items
                for out in items_remaining:
                    self._write(out.data)
                    col_flushed += visual_width(out.data, col_flushed)
                break

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
