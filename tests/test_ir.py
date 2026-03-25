"""Tests for the document IR resolver."""

import unittest

from zeekscript.ir import (
    HARDLINE, LINE, SOFTLINE, SPACE, EMPTY,
    Text, align, concat, fill, group, if_break, intersperse,
    join, nest, resolve, text,
)


class TestIR(unittest.TestCase):
    """Test the document IR primitives and resolver."""

    def _resolve(self, doc, max_width=80):
        return resolve(doc, max_width=max_width).decode()

    def test_simple_text(self):
        doc = text("hello")
        self.assertEqual(self._resolve(doc), "hello\n")

    def test_concat(self):
        doc = concat(text("hello"), SPACE, text("world"))
        self.assertEqual(self._resolve(doc), "hello world\n")

    def test_concat_flattens(self):
        inner = concat(text("a"), text("b"))
        outer = concat(inner, text("c"))
        # Should flatten to ("a", "b", "c"), not nest
        self.assertIsInstance(outer, type(inner))

    def test_hardline(self):
        doc = concat(text("line1"), HARDLINE, text("line2"))
        self.assertEqual(self._resolve(doc), "line1\nline2\n")

    def test_group_fits_flat(self):
        doc = group(concat(text("a"), LINE, text("b")))
        self.assertEqual(self._resolve(doc, max_width=10), "a b\n")

    def test_group_breaks(self):
        doc = group(concat(text("a"), LINE, text("b")))
        self.assertEqual(self._resolve(doc, max_width=2), "a\nb\n")

    def test_softline_flat(self):
        doc = group(concat(text("a"), SOFTLINE, text("b")))
        self.assertEqual(self._resolve(doc, max_width=10), "ab\n")

    def test_softline_break(self):
        doc = group(concat(text("a"), SOFTLINE, text("b")))
        self.assertEqual(self._resolve(doc, max_width=1), "a\nb\n")

    def test_nest(self):
        doc = group(concat(text("if"), LINE, nest(1, concat(text("body")))))
        # Fits on one line
        self.assertEqual(self._resolve(doc, max_width=20), "if body\n")

    def test_nest_breaks(self):
        doc = concat(
            text("if (cond)"),
            nest(1, concat(HARDLINE, text("body_statement;")))
        )
        self.assertEqual(self._resolve(doc), "if (cond)\n\tbody_statement;\n")

    def test_align_continuation(self):
        # function_call(arg1,
        #               arg2)
        doc = group(concat(
            text("function_call("),
            align(concat(
                text("arg1"),
                concat(text(","), LINE),
                text("arg2"),
                text(")"),
            )),
        ))
        result = self._resolve(doc, max_width=24)
        lines = result.strip().split("\n")
        self.assertEqual(len(lines), 2)
        self.assertEqual(lines[0], "function_call(arg1,")
        self.assertEqual(lines[1], "              arg2)")

    def test_align_inside_nest(self):
        # Inside a function body (1 tab), alignment uses tab + spaces:
        # {
        #     call(arg1,
        #          arg2)
        doc = concat(
            text("{"),
            nest(1, concat(
                HARDLINE,
                group(concat(
                    text("call("),
                    align(concat(
                        text("arg1"),
                        concat(text(","), LINE),
                        text("arg2"),
                        text(")"),
                    )),
                )),
            )),
        )
        result = self._resolve(doc, max_width=20)
        lines = result.strip().split("\n")
        self.assertEqual(len(lines), 3)
        self.assertEqual(lines[0], "{")
        self.assertEqual(lines[1], "\tcall(arg1,")
        self.assertEqual(lines[2], "\t     arg2)")

    def test_if_break(self):
        doc = group(concat(
            text("["),
            if_break(
                broken=concat(HARDLINE, text("  item")),
                flat=text("item"),
            ),
            text("]"),
        ))
        # Fits flat
        self.assertEqual(self._resolve(doc, max_width=20), "[item]\n")
        # Breaks
        result = self._resolve(doc, max_width=5)
        self.assertIn("\n", result.strip())

    def test_fill_fits_one_line(self):
        items = [text("a"), text("b"), text("c")]
        sep = concat(text(","), LINE)
        doc = fill(*intersperse(sep, items))
        self.assertEqual(self._resolve(doc, max_width=20), "a, b, c\n")

    def test_fill_wraps(self):
        items = [text("aaa"), text("bbb"), text("ccc"), text("ddd")]
        sep = concat(text(","), LINE)
        doc = fill(*intersperse(sep, items))
        result = self._resolve(doc, max_width=12)
        lines = result.strip().split("\n")
        # Should pack as many as fit per line
        self.assertEqual(lines[0], "aaa, bbb,")
        self.assertEqual(lines[1], "ccc, ddd")

    def test_fill_with_align(self):
        # fn(aaa, bbb,
        #    ccc, ddd)
        items = [text("aaa"), text("bbb"), text("ccc"), text("ddd")]
        sep = concat(text(","), LINE)
        doc = concat(
            text("fn("),
            align(concat(
                fill(*intersperse(sep, items)),
                text(")"),
            )),
        )
        result = self._resolve(doc, max_width=13)
        lines = result.strip().split("\n")
        self.assertEqual(lines[0], "fn(aaa, bbb,")
        self.assertEqual(lines[1], "   ccc, ddd)")

    def test_nested_groups(self):
        # Outer group breaks, inner group stays flat
        inner = group(concat(text("f("), text("x"), text(")")))
        doc = group(concat(
            text("outer("),
            align(concat(
                text("arg1"),
                concat(text(","), LINE),
                inner,
                text(")"),
            )),
        ))
        result = self._resolve(doc, max_width=16)
        lines = result.strip().split("\n")
        self.assertEqual(len(lines), 2)
        self.assertIn("f(x)", lines[1])

    def test_trailing_whitespace_stripped(self):
        doc = concat(text("hello   "), HARDLINE, text("world"))
        result = self._resolve(doc)
        self.assertNotIn("   \n", result)
        self.assertIn("hello\n", result)

    def test_join(self):
        doc = join(SPACE, [text("a"), text("b"), text("c")])
        self.assertEqual(self._resolve(doc), "a b c\n")

    def test_empty_concat(self):
        doc = concat()
        self.assertEqual(doc, EMPTY)

    def test_single_concat(self):
        doc = concat(text("only"))
        self.assertIsInstance(doc, Text)


if __name__ == "__main__":
    unittest.main()
