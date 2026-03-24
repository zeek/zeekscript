"""Functionality related to output."""

import contextlib
import os
import sys
from collections.abc import Generator
from types import TracebackType
from typing import Any, BinaryIO, NamedTuple, TextIO

from .formatter import Formatter, Hint


class BreakPoint(NamedTuple):
    """A candidate position for breaking a long line."""
    index: int             # Index into the items list
    visual_column: int     # Visual column after this token
    nesting_depth: int     # Paren/bracket/brace nesting depth
    is_comma: bool         # Token is a comma
    is_before_good_lb: bool  # Next non-whitespace token has GOOD_AFTER_LB
    is_preferred_op: bool  # Token is && or ||


class FilteredBreak(NamedTuple):
    """A break point after filtering, with reduced fields."""
    index: int
    visual_column: int
    is_before_good_lb: bool


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


class LineBreaker:
    """Handles line-wrapping logic for a single buffered line.

    Iteratively breaks lines that exceed MAX_LINE_LEN (80 columns):

    1. Find valid break points (after tokens without NO_LB_AFTER hint)
    2. Filter to prefer breaks at outermost nesting level and after commas
    3. Prefer breaks before GOOD_AFTER_LB tokens (keeps operators at line end)
    4. Choose a break that balances line lengths while keeping both under limit
    5. Skip breaks that would make things worse (line2 >= original length)
    6. If no break keeps both lines under limit, minimize line1's excess
    7. Repeat for remainder until all lines fit or no valid breaks remain
    """

    def __init__(self, stream: "OutputStream", items: list[Output]) -> None:
        self.stream = stream
        self.col_flushed = 0
        self.tbd: list[Output] = []
        self.break_align_col = 0
        self.current_depth = 0
        self.items_remaining = items

    @staticmethod
    def visual_width(data: bytes, start_col: int, tab_size: int) -> int:
        """Calculate visual width of data, accounting for tab stops."""
        col = start_col
        for byte in data:
            if byte == ord(b"\t"):
                col = ((col // tab_size) + 1) * tab_size
            else:
                col += 1
        return col - start_col

    def _write_linebreak(self) -> None:
        s = self.stream
        total_indent = s._tab_indent + s._preproc_depth
        s._write(Formatter.NL)
        s._write(b"\t" * total_indent)
        tab_col = total_indent * s.TAB_SIZE

        # Check if first non-whitespace item needs special handling
        align_col = self.break_align_col
        use_tab_wrap = False
        for item in self.tbd:
            if item.data.strip():
                if item.hints is not None and Hint.INIT_ELEMENT in item.hints:
                    use_tab_wrap = True
                break

        if use_tab_wrap:
            # Initializer elements use an extra tab for continuation
            s._write(b"\t")
            self.col_flushed = tab_col + s.TAB_SIZE
        elif align_col > 0 and align_col < s.MAX_ALIGN_COL:
            # Align to the specified column (e.g., after opening paren)
            # Skip if alignment uses more than half the line (too much whitespace)
            space_count = max(0, align_col - tab_col)
            s._write(b" " * space_count)
            self.col_flushed = tab_col + space_count
        elif align_col >= s.MAX_ALIGN_COL:
            # Alignment uses too much whitespace, use TAB_SIZE
            # to maintain visual hierarchy with parent constructs
            s._write(b" " * s.TAB_SIZE)
            self.col_flushed = tab_col + s.TAB_SIZE
        else:
            # align_col == 0: no alignment set - this shouldn't happen.
            s._write(s.MISINDENT_MARKER)
            s._write(b"\t" * total_indent)
            self.col_flushed = tab_col

        # Remove leading whitespace from continuation
        while self.tbd and not self.tbd[0].data.strip():
            self.tbd.pop(0)

    def find_break_points(
        self, items: list[Output], start_col: int, start_depth: int = 0
    ) -> tuple[list[BreakPoint], int, bool, int]:
        """Find valid break points in a list of items.

        Returns (all_break_points, total_len, has_init_lenient, end_depth).
        """
        tab_size = self.stream.TAB_SIZE
        total_len = start_col
        all_break_points: list[BreakPoint] = []
        has_init_lenient = False
        nesting_depth = start_depth

        # First pass: collect indices of tokens with GOOD_AFTER_LB
        good_lb_indices: set[int] = set()
        for i, out in enumerate(items):
            if out.data.strip() and Hint.GOOD_AFTER_LB in out.formatter.hints:
                good_lb_indices.add(i)

        for i, out in enumerate(items):
            token = out.data.strip()
            if token in (b"(", b"[", b"{"):
                nesting_depth += 1
            elif token in (b")", b"]", b"}"):
                nesting_depth -= 1

            if Hint.ZERO_WIDTH not in out.formatter.hints:
                # Exclude newlines from line length calculation
                if out.data != b'\n' and out.data != b'\r\n':
                    total_len += self.visual_width(out.data, total_len, tab_size)

            if token:
                if out.hints is not None and Hint.INIT_LENIENT in out.hints:
                    has_init_lenient = True
                if Hint.NO_LB_AFTER not in out.formatter.hints:
                    is_comma = token == b","
                    # Check if current token is a preferred logical operator.
                    is_preferred_op = token in (b"&&", b"||")
                    # Check if the next non-whitespace token has GOOD_AFTER_LB
                    # or is a comma (which suppresses this break point)
                    is_before_good_lb = False
                    next_is_comma = False
                    for j in range(i + 1, len(items)):
                        next_token = items[j].data.strip()
                        if next_token:
                            is_before_good_lb = j in good_lb_indices
                            next_is_comma = next_token == b","
                            break
                    # Don't register a break point right before a comma —
                    # the comma itself is a better break point.
                    if not next_is_comma:
                        all_break_points.append(BreakPoint(
                            i, total_len, nesting_depth, is_comma,
                            is_before_good_lb, is_preferred_op,
                        ))

        return all_break_points, total_len, has_init_lenient, nesting_depth

    @staticmethod
    def _as_filtered(bp: BreakPoint) -> FilteredBreak:
        return FilteredBreak(bp.index, bp.visual_column, bp.is_before_good_lb)

    @staticmethod
    def filter_break_points(
        all_break_points: list[BreakPoint],
    ) -> list[FilteredBreak]:
        """Filter break points by nesting depth, comma preference, and GOOD_AFTER_LB.

        Prefers breaks at the minimum non-zero nesting depth to avoid breaking
        inside nested function calls when an outer break is available.
        At each depth, prefers breaks in this order:
        1. Commas (for argument lists)
        2. Preferred operators (&&, ||) - logical structure
        3. All other breaks at that depth

        GOOD_AFTER_LB breaks from outer depths are included as fallback options,
        but GOOD_AFTER_LB breaks at the same depth as commas are excluded
        (prevents arithmetic operators competing with commas).
        """
        as_filtered = LineBreaker._as_filtered

        if not all_break_points:
            return []

        depths = sorted(set(bp.nesting_depth for bp in all_break_points))
        non_zero_depths = [d for d in depths if d > 0]
        target_depths = non_zero_depths if non_zero_depths else depths

        for depth in target_depths:
            at_depth = [bp for bp in all_break_points if bp.nesting_depth == depth]
            if at_depth:
                # Priority: commas > preferred ops (&&, ||) > all other breaks
                comma_breaks = [as_filtered(bp) for bp in at_depth if bp.is_comma]
                if comma_breaks:
                    # Only include GOOD_AFTER_LB breaks from OUTER depths as fallbacks.
                    outer_good_lb = [
                        as_filtered(bp) for bp in all_break_points
                        if bp.is_before_good_lb and bp.nesting_depth < depth
                    ]
                    return comma_breaks + outer_good_lb

                preferred_breaks = [as_filtered(bp) for bp in at_depth if bp.is_preferred_op]
                if preferred_breaks:
                    return preferred_breaks

                # No commas or preferred ops - include all GOOD_AFTER_LB as options
                good_lb_breaks = [
                    as_filtered(bp) for bp in all_break_points if bp.is_before_good_lb
                ]
                return [as_filtered(bp) for bp in at_depth] + good_lb_breaks

        # Fallback: return all GOOD_AFTER_LB breaks
        return [LineBreaker._as_filtered(bp) for bp in all_break_points if bp.is_before_good_lb]

    def choose_break(
        self,
        items: list[Output],
        break_points: list[FilteredBreak],
        total_len: int,
        start_col: int,
        continuation_indent: int,
        effective_max_len: int,
    ) -> int:
        """Choose the best break point index, or -1 if no break needed.

        Strategy:
        1. If line fits, no break needed
        2. Find breaks where both line1 and line2 fit under MAX_LINE_LEN
        3. Among valid breaks, prefer those before GOOD_AFTER_LB tokens
        4. Among remaining valid breaks, prefer the latest where line2 >= 2/3
           of line1 (balances lines while maximizing content on line1)
        5. If no break keeps both lines under limit, pick the break that
           keeps line1 closest to MAX_LINE_LEN (minimizing excess)
        6. Skip breaks that aren't "worth it"
        7. If chosen break isn't worthwhile, try alternative break points
        """
        s = self.stream
        tab_size = s.TAB_SIZE

        if total_len <= effective_max_len:
            return -1
        num_items = len([o for o in items if o.data.strip()])
        has_good_lb = any(bp.is_before_good_lb for bp in break_points)
        significantly_over = total_len > effective_max_len + 20
        if num_items < s.MIN_LINE_ITEMS and not has_good_lb and not significantly_over:
            return -1
        if not break_points:
            return -1

        # Adjust break point columns relative to start_col
        adjusted_breaks = [
            FilteredBreak(bp.index, bp.visual_column - start_col, bp.is_before_good_lb)
            for bp in break_points
        ]

        # Find breaks where both lines fit under limit
        valid_breaks: list[tuple[int, int, int, bool]] = []
        for bp in adjusted_breaks:
            idx, col, is_good = bp.index, bp.visual_column, bp.is_before_good_lb
            abs_col = start_col + col
            remainder = total_len - abs_col
            # Get the actual alignment for the item after this break
            break_cont = continuation_indent
            for j in range(idx + 1, len(items)):
                if items[j].data.strip():
                    if items[j].align_column > 0:
                        break_cont = items[j].align_column
                    break
            line2_len = break_cont + remainder
            if abs_col <= s.MAX_LINE_LEN and line2_len <= s.MAX_LINE_LEN:
                valid_breaks.append((idx, abs_col, line2_len, is_good))

        if not valid_breaks:
            # No break keeps both lines under limit.
            # Pick break that keeps line1 closest to 80.
            target = s.MAX_LINE_LEN

            def score(bp: FilteredBreak) -> tuple[int, int]:
                abs_col = start_col + bp.visual_column
                if abs_col <= target:
                    return (0, target - abs_col)
                else:
                    return (1, abs_col - target)

            best_bp = min(adjusted_breaks, key=score)
            best_break = best_bp.index
        else:
            # Prefer breaks before GOOD_AFTER_LB tokens
            good_lb_breaks = [vb for vb in valid_breaks if vb[3]]
            candidates = good_lb_breaks if good_lb_breaks else valid_breaks

            # Among candidates, prefer latest where line2 >= 2/3 of line1
            best_break = -1
            for idx, abs_col, line2_len, _ in reversed(candidates):
                if line2_len >= abs_col * 2 // 3:
                    best_break = idx
                    break
            if best_break < 0:
                best_break = min(candidates, key=lambda x: abs(x[1] - x[2]))[0]

        # Check if break is worth it
        if best_break >= 0:
            break_col = next(
                start_col + bp.visual_column for bp in adjusted_breaks if bp.index == best_break
            )
            remainder = total_len - break_col
            if remainder < s.MIN_LINE_EXCESS and total_len <= s.MAX_LINE_LEN + s.MIN_LINE_EXCESS:
                return -1

        # Check there's content after the break
        if best_break >= 0:
            has_content = any(items[j].data.strip() for j in range(best_break + 1, len(items)))
            if not has_content:
                return -1

        # Check if breaking would actually help
        def is_break_worthwhile(break_idx: int) -> bool:
            remaining_items = items[break_idx + 1:]
            while remaining_items and not remaining_items[0].data.strip():
                remaining_items = remaining_items[1:]
            if not remaining_items:
                return False
            cont_align = continuation_indent
            if remaining_items[0].align_column > 0:
                cont_align = remaining_items[0].align_column
            remaining_len = sum(self.visual_width(item.data, 0, tab_size) for item in remaining_items)
            line2_len = cont_align + remaining_len
            if line2_len > total_len:
                return False
            if line2_len == total_len:
                first_token = remaining_items[0].data.strip()
                if first_token.startswith(b'"') or first_token.startswith(b"'"):
                    return False
                remaining_bps = [bp for bp in adjusted_breaks if bp.index > break_idx]
                for bp in remaining_bps:
                    token = items[bp.index].data.strip()
                    if token not in (b")", b"]", b"}", b";"):
                        return True
                return False
            return True

        # Validate the chosen break, try alternatives if not worthwhile
        if best_break >= 0 and not is_break_worthwhile(best_break):
            for bp in adjusted_breaks:
                if bp.index != best_break and is_break_worthwhile(bp.index):
                    best_break = bp.index
                    break
            else:
                best_break = -1

        # Final check: ensure there's content after the break
        if best_break >= 0:
            has_content = any(items[j].data.strip() for j in range(best_break + 1, len(items)))
            if not has_content:
                return -1

        return best_break

    def run(self) -> None:
        """Process the line buffer, breaking lines as needed."""
        s = self.stream
        tab_size = s.TAB_SIZE

        # Propagate NO_LB_BEFORE backward as NO_LB_AFTER
        needs_no_lb_after = False
        for out in self.items_remaining[::-1]:
            if out.data.strip() and needs_no_lb_after:
                if out.data.strip() != b",":
                    out.formatter.hints |= Hint.NO_LB_AFTER
                needs_no_lb_after = False
            if Hint.NO_LB_BEFORE in out.formatter.hints:
                needs_no_lb_after = True

        while self.items_remaining:
            all_bps, total_len, has_init_lenient, end_depth = self.find_break_points(
                self.items_remaining, self.col_flushed, self.current_depth
            )
            break_points = self.filter_break_points(all_bps)

            effective_max_len = s.MAX_LINE_LEN * 2 if has_init_lenient else s.MAX_LINE_LEN

            # Determine continuation indent for potential breaks
            total_indent = s._tab_indent + s._preproc_depth
            tab_col = total_indent * s.TAB_SIZE
            cont_indent = 0
            if break_points:
                first_break_idx = break_points[0].index
                for j in range(first_break_idx + 1, len(self.items_remaining)):
                    if self.items_remaining[j].data.strip():
                        cont_indent = self.items_remaining[j].align_column
                        break
            if cont_indent == 0:
                cont_indent = tab_col + s.SPACE_INDENT
            elif cont_indent >= s.MAX_ALIGN_COL:
                cont_indent = tab_col + s.TAB_SIZE

            chosen_break = self.choose_break(
                self.items_remaining, break_points, total_len, self.col_flushed,
                cont_indent, effective_max_len
            )

            # If no valid break with filtered points, or if chosen break still
            # produces an over-limit line1, try all break points.
            try_all_breaks = chosen_break < 0 and total_len > effective_max_len
            if not try_all_breaks and chosen_break >= 0:
                for bp in all_bps:
                    if bp.index == chosen_break:
                        if bp.visual_column > effective_max_len:
                            try_all_breaks = True
                        break
            if try_all_breaks:
                all_break_tuples = [self._as_filtered(bp) for bp in all_bps]
                chosen_break = self.choose_break(
                    self.items_remaining, all_break_tuples, total_len,
                    self.col_flushed, cont_indent, effective_max_len
                )

            # Avoid orphan operators
            if chosen_break >= 0:
                remainder = self.items_remaining[chosen_break + 1:]
                first_idx = None
                for ri, item in enumerate(remainder):
                    if item.data.strip():
                        first_idx = ri
                        break
                if first_idx is not None:
                    first_token = remainder[first_idx].data.strip()
                    if first_token in (b"*", b"+", b"-", b"/", b"%", b"&", b"|", b"^"):
                        op_abs_idx = chosen_break + 1 + first_idx
                        next_bp = None
                        for bp in all_bps:
                            if bp.index == op_abs_idx:
                                next_bp = bp
                                break
                        if next_bp is not None:
                            chosen_break = op_abs_idx

            if chosen_break >= 0:
                # Get nesting depth at break point for next iteration
                depth_at_break = self.current_depth
                for bp in all_bps:
                    if bp.index == chosen_break:
                        depth_at_break = bp.nesting_depth
                        break

                # Output items up to and including break point
                for out in self.items_remaining[: chosen_break + 1]:
                    s._write(out.data)
                    self.col_flushed += self.visual_width(out.data, self.col_flushed, tab_size)

                self.current_depth = depth_at_break
                self.items_remaining = self.items_remaining[chosen_break + 1 :]

                # Remove leading whitespace from remaining
                removed_ws_width = 0
                while self.items_remaining and not self.items_remaining[0].data.strip():
                    removed_ws_width += len(self.items_remaining[0].data)
                    self.items_remaining.pop(0)

                # Find alignment column from first remaining item
                self.break_align_col = 0
                if self.items_remaining:
                    self.break_align_col = self.items_remaining[0].align_column

                # Adjust nested alignments
                for item in self.items_remaining:
                    if item.align_column > self.break_align_col:
                        item.align_column = (
                            self.break_align_col
                            + (item.align_column - self.col_flushed - removed_ws_width)
                        )

                # Check if remaining content would fit with shallower alignment
                if self.break_align_col > 0:
                    break_tokens = (b",", b"/", b"*", b"+", b"-", b"||", b"&&")
                    depth = 0
                    has_outer_breaks = False
                    seen_first_token = False
                    for item in self.items_remaining:
                        token = item.data.strip()
                        if token in (b"(", b"[", b"{"):
                            depth += 1
                        elif token in (b")", b"]", b"}"):
                            depth -= 1
                        elif token in break_tokens:
                            # Break tokens at any depth indicate the line-breaker
                            # can wrap further in subsequent iterations.
                            has_outer_breaks = True
                            break
                        elif (depth <= 0 and token and seen_first_token
                              and Hint.GOOD_AFTER_LB in item.formatter.hints):
                            has_outer_breaks = True
                            break
                        if token:
                            seen_first_token = True
                    if not has_outer_breaks:
                        remaining_len = sum(
                            self.visual_width(item.data, 0, tab_size)
                            for item in self.items_remaining
                        )
                        line2_len = self.break_align_col + remaining_len
                        if line2_len > s.MAX_LINE_LEN:
                            target_end = s.MAX_LINE_LEN - 2
                            shallow_align = max(tab_col + s.SPACE_INDENT,
                                              target_end - remaining_len)
                            if shallow_align + remaining_len <= s.MAX_LINE_LEN:
                                self.break_align_col = shallow_align

                # Write linebreak
                self.tbd = self.items_remaining
                self._write_linebreak()
            else:
                # No break needed - output all remaining items
                for out in self.items_remaining:
                    s._write(out.data)
                    self.col_flushed += self.visual_width(out.data, self.col_flushed, tab_size)
                break


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
    # Maximum alignment column before falling back to simpler indentation.
    # Alignments beyond this produce too much leading whitespace.
    MAX_ALIGN_COL = 60

    # Marker emitted when continuation alignment is missing.
    MISINDENT_MARKER = b"# MISINDENTATION\n"

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

        # Depth of preprocessor conditional blocks (@if/@ifdef/@ifndef).
        # Used to indent content inside these blocks at top level.
        self._preproc_depth: int = 0

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

    def enter_preproc_block(self) -> None:
        """Enter a preprocessor conditional block (@if/@ifdef/@ifndef).

        Only increments depth at top level (tab_indent == 0) so that
        content inside top-level @if blocks gets indented, but content
        inside @if blocks within functions doesn't get extra indentation.
        """
        if self._tab_indent == 0:
            self._preproc_depth += 1

    def exit_preproc_block(self) -> None:
        """Exit a preprocessor conditional block (@endif)."""
        if self._preproc_depth > 0:
            self._preproc_depth -= 1

    def set_align_column(self, column: int) -> None:
        """Set the column to align to on linebreaks (0 to use default)."""
        self._align_column = column

    def get_align_column(self) -> int:
        """Get the current alignment column."""
        return self._align_column

    @contextlib.contextmanager
    def aligned_to(self, column: int) -> Generator[None]:
        """Context manager that sets alignment column and restores it on exit."""
        saved = self._align_column
        self._align_column = column
        try:
            yield
        finally:
            self._align_column = saved

    @contextlib.contextmanager
    def aligned_to_if_unset(self, column: int) -> Generator[None]:
        """Like aligned_to, but only sets alignment if not already set (non-zero)."""
        if self._align_column == 0:
            with self.aligned_to(column):
                yield
        else:
            yield

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
        # Always apply preproc_depth indentation, even when tab indent is disabled.
        # This ensures @load etc inside @if blocks still get indented.
        if self._use_tab_indent:
            self._tab_indent = formatter.indent
            total_indent = self._tab_indent + self._preproc_depth
            self.write(b"\t" * total_indent, formatter)
        elif self._preproc_depth > 0:
            self.write(b"\t" * self._preproc_depth, formatter)

    def write_space_align(self, formatter: Formatter) -> None:
        if self._use_space_align:
            # Align to _align_column if set
            if self._align_column > 0:
                current_col = self.get_visual_column()
                spaces_needed = self._align_column - current_col
                if spaces_needed > 0:
                    self.write(b" " * spaces_needed, formatter)
                    return
            # No alignment - flag with MISINDENTATION marker as comment on its own line.
            # Uses self.write() since we're in the buffering phase.
            self.write(self.MISINDENT_MARKER, formatter)
            # Re-write the tab indent for the actual content
            self.write_tab_indent(formatter)

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
        """Flushes out the line buffer, applying line wrapping as needed."""
        # Without linebreaking active, just flush the buffer.
        if not self._enable_linebreaks or not self._use_linebreaks:
            for out in self._linebuffer:
                self._write(out.data)
        else:
            breaker = LineBreaker(self, list(self._linebuffer))
            breaker.run()

        self._linebuffer = []
        self._col = 0
        # Preserve alignment for midline continuations (e.g., after annotation comments)
        if not self._use_space_align:
            self._align_column = 0

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
