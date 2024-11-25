#!/usr/bin/env python
"""Tests for the zeekscript.Script class."""

import io
import unittest

# Sets up sys.path and provides helpers
import testutils as tu

# This imports the tree-local zeekscript
from testutils import zeekscript


class TestScript(unittest.TestCase):
    def setUp(self):
        # This prints large diffs in case assertEqual() finds discrepancies.
        self.maxDiff = None

    def assertTreeBinary(self, script, baseline, include_cst=False):
        buf = io.BytesIO()
        script.write_tree(output=buf, include_cst=include_cst)
        self.assertEqual(tu.normalize(baseline), buf.getvalue())

    def assertTreeText(self, script, baseline, include_cst=False):
        buf = io.StringIO()
        script.write_tree(output=buf, include_cst=include_cst)
        self.assertEqual(tu.fix_lineseps(baseline), buf.getvalue())

    def test_write_ast(self):
        script_data = "event zeek_init() { }\n"

        baseline = """source_file (0.0,1.0) 'event zeek_init() { }\\n'
    decl (0.0,0.21) 'event zeek_init() { }'
        func_decl (0.0,0.21) 'event zeek_init() { }'
            func_hdr (0.0,0.17) 'event zeek_init()'
                event (0.0,0.17) 'event zeek_init()'
                    event (0.0,0.5)
                    id (0.6,0.15) 'zeek_init'
                    func_params (0.15,0.17) '()'
                        ( (0.15,0.16)
                        ) (0.16,0.17)
            func_body (0.18,0.21) '{ }'
                { (0.18,0.19)
                } (0.20,0.21)
"""
        script = zeekscript.Script(io.StringIO(script_data))

        self.assertTrue(script.parse())
        self.assertTreeBinary(script, baseline)

    def test_write_ast_text(self):
        script_data = "event zeek_init() { }\n"

        baseline = """source_file (0.0,1.0) 'event zeek_init() { }\\n'
    decl (0.0,0.21) 'event zeek_init() { }'
        func_decl (0.0,0.21) 'event zeek_init() { }'
            func_hdr (0.0,0.17) 'event zeek_init()'
                event (0.0,0.17) 'event zeek_init()'
                    event (0.0,0.5)
                    id (0.6,0.15) 'zeek_init'
                    func_params (0.15,0.17) '()'
                        ( (0.15,0.16)
                        ) (0.16,0.17)
            func_body (0.18,0.21) '{ }'
                { (0.18,0.19)
                } (0.20,0.21)
"""
        script = zeekscript.Script(io.StringIO(script_data))

        self.assertTrue(script.parse())
        self.assertTreeText(script, baseline)

    def test_write_cst(self):
        script_data = """# A comment.
event zeek_init() { }
"""
        baseline = """source_file (0.0,2.0) '# A comment.\\nevent zeek_init() { }\\n'
    v minor_comment (0.0,0.12) '# A comment.'
    v nl (0.12,1.0) '\\n'
    decl (1.0,1.21) 'event zeek_init() { }'
        func_decl (1.0,1.21) 'event zeek_init() { }'
            func_hdr (1.0,1.17) 'event zeek_init()'
                event (1.0,1.17) 'event zeek_init()'
                    event (1.0,1.5)
                    id (1.6,1.15) 'zeek_init'
                    func_params (1.15,1.17) '()'
                        ( (1.15,1.16)
                        ) (1.16,1.17)
            func_body (1.18,1.21) '{ }'
                { (1.18,1.19)
                } (1.20,1.21)
                ^ nl (1.21,2.0) '\\n'
"""
        script = zeekscript.Script(io.StringIO(script_data))

        self.assertTrue(script.parse())
        self.assertTreeBinary(script, baseline, include_cst=True)
