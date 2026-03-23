#!/usr/bin/env python
"""Formatting-related tests."""

import io
import os
import pathlib
import sys
import textwrap
import unittest

# Sets up sys.path and provides helpers
import testutils as tu

# This imports the tree-local zeekscript
from testutils import zeekscript


class TestFormatting(unittest.TestCase):
    def _format(self, content):
        script = zeekscript.Script(io.BytesIO(content))

        self.assertTrue(script.parse())
        self.assertFalse(script.has_error())

        buf = io.BytesIO()
        script.format(buf)

        return buf.getvalue()

    def test_interval(self):
        self.assertEqual(self._format(b"1 sec;").rstrip(), b"1 sec;")
        self.assertEqual(self._format(b"1min;").rstrip(), b"1 min;")
        self.assertEqual(self._format(b"3.5  hrs;").rstrip(), b"3.5 hrs;")

    def test_index_slice(self):
        # Use compact layout if neither side is a constant or id.
        self.assertEqual(self._format(b"xs[  0   :1  ];").rstrip(), b"xs[0:1];")

        # # If either side is unspecified do not use use compact layout, but do
        # not add additional whitespace if both sides are omitted.
        self.assertEqual(self._format(b"xs[  0   :  ];").rstrip(), b"xs[0:];")
        self.assertEqual(self._format(b"xs[  : 1 ];").rstrip(), b"xs[:1];")
        self.assertEqual(self._format(b"data[1-1:];").rstrip(), b"data[1 - 1 :];")
        self.assertEqual(self._format(b"data[:1-1];").rstrip(), b"data[: 1 - 1];")
        self.assertEqual(self._format(b"data[   :   ];").rstrip(), b"data[:];")

        # Else surround `:` with spaces.
        self.assertEqual(self._format(b"data[1-1:1];").rstrip(), b"data[1 - 1 : 1];")
        self.assertEqual(self._format(b"data[1 :1-1];").rstrip(), b"data[1 : 1 - 1];")
        self.assertEqual(self._format(b"data[f(): ];").rstrip(), b"data[f() :];")

    def test_format_comment_separator(self):
        """Validates that we preserve the separator before a comment. This is a
        regression test for #62."""

        self.assertEqual(
            self._format(b"global x = 42;   # Inline.").rstrip(),
            b"global x = 42; # Inline.",
        )

        code = textwrap.dedent(
            """\
               event zeek_init() {}
               # Comment on next line.
            """
        )

        expected = textwrap.dedent(
            """\
            event zeek_init()
            \t{ }
            # Comment on next line.
            """
        )

        # We split out lines here to work around different line endings on Windows.
        self.assertEqual(
            self._format(code.encode("UTF-8")).decode().splitlines(),
            expected.splitlines(),
        )

    def test_init_lists(self):
        # Square brackets should have no spaces after [ and before ]
        self.assertEqual(self._format(b"[ ];").rstrip(), b"[];")
        self.assertEqual(self._format(b"[ 1 ];").rstrip(), b"[1];")
        self.assertEqual(self._format(b"[ 1 , 2 ];").rstrip(), b"[1, 2];")

        # Trailing comma
        self.assertEqual(self._format(b"[ 1 , ];").rstrip(), b"[1, ];")
        self.assertEqual(self._format(b"[ 1 , 2 , ];").rstrip(), b"[1, 2, ];")

        self.assertEqual(self._format(b"local x = {};").rstrip(), b"local x = set();")
        self.assertEqual(
            self._format(b'global x = { [ 1 ] = "one" };').rstrip(),
            b'global x = table([1] = "one");',
        )

        # Test complex nodes
        code = textwrap.dedent("""\
            global x = [1, # Comment one
            2 # Comment two
            ];
            """)

        expected = textwrap.dedent("""\
            global x = [
            \t1, # Comment one
            \t2 # Comment two
            ];
            """)

        # We split out lines here to work around different line endings on Windows.
        self.assertEqual(
            self._format(code.encode("UTF-8")).decode().splitlines(),
            expected.splitlines(),
        )

    def test_no_split_on_field_access(self):
        """Don't split on $ or ?$ operators - keep expr$field together."""
        # The $ operator should not be a break point
        code = b"if ( some_long_bool_variable_a && result$result_string == \"ok\" && result?$chain_certs && |result$chain_certs| > 2 ) print \"x\";"
        result = self._format(code).decode()
        # Verify $result_string stays together
        self.assertIn("result$result_string", result)
        # Verify ?$chain_certs stays together (not split as "result\n?$chain_certs")
        self.assertIn("result?$chain_certs", result)
        self.assertNotIn("result\n", result.replace("result$", "").replace("result?", ""))

    def test_assignment_line_breaking(self):
        """Assignment breaks should not create unnecessary splits."""
        # When RHS needs breaking anyway, don't also break at =
        code = b"age_d = interval_to_double(network_time() - cert$not_valid_before) / 86400.0;"
        result = self._format(code).decode()
        # Should keep "age_d = interval_to_double(network_time() -" on one line
        # Not break as "age_d =\n    interval_to_double..."
        self.assertIn("age_d = interval_to_double(network_time() -", result)

    def test_assignment_break_when_rhs_fits(self):
        """Assignment can break at = when RHS fits on continuation line."""
        # += should get proper indentation, not MISINDENTATION
        code = b"another_very_long_access_to_some_member += yetanotherveryveryveryverylongthing[0];"
        result = self._format(code).decode()
        # Should not have MISINDENTATION marker
        self.assertNotIn("MISINDENTATION", result)

    def test_event_handler_parameter_wrapping(self):
        """Event handler parameters should wrap at commas when line is too long."""
        code = b'event some_encrypted_data(c: connection, is_orig: bool, some_record_vers: count, some_content_t: count, length: count) &group="some-group" { }'
        result = self._format(code).decode()
        lines = result.splitlines()
        # Parameters should wrap - not all on one line
        # First line should have some parameters, second line should have more
        self.assertTrue(
            any("some_content_t:" in line for line in lines[1:]),
            "Parameters should wrap to multiple lines"
        )
        # The &group attribute should be on its own line or with closing params
        self.assertTrue(
            any("&group" in line for line in lines),
            "Attribute should be present"
        )

    def test_event_attr_stays_inline_when_short(self):
        """Short event header with &group fits on one line."""
        code = b"event foo(c: connection) &group=bar { print c; }"
        result = self._format(code).decode()
        lines = result.splitlines()
        self.assertIn("&group=bar", lines[0])

    def test_event_attr_own_line_when_params_wrap(self):
        """When params wrap, &group goes on its own continuation line."""
        code = (
            b"event SomeModule::some_raised_evt(c: connection, is_orig: bool,"
            b" version: count, some_dcid: string, some_scid: string,"
            b" some_retoken: string, some_integ_tag: string)"
            b" &group=SomeEvtGroup { print c; }"
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        # &group should be on its own line, not glued to closing paren
        attr_line = [l for l in lines if "&group" in l]
        self.assertEqual(len(attr_line), 1)
        self.assertNotIn(")", attr_line[0])
        # Params should align at same column
        paren_col = lines[0].index("(") + 1
        param_line = lines[1]
        self.assertEqual(len(param_line) - len(param_line.lstrip()), paren_col)

    def test_event_attr_underindented_when_deep_align_overflows(self):
        """When param alignment + attr_list > 80, under-indent the attr_list."""
        code = (
            b"event some_generic_packet_threshold_crossed(c: connection,"
            b" threshold: count)"
            b" &group=MyPkg_SomeUnknownprotos_EvtGroup { print c; }"
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        attr_line = [l for l in lines if "&group" in l]
        self.assertEqual(len(attr_line), 1)
        self.assertLessEqual(len(attr_line[0]), 80)

    def test_ifdef_wrapped_params_use_tab_indent(self):
        """Wrapped param continuation inside @ifdef uses tab+spaces, not all spaces."""
        code = (
            b"@ifdef ( SOME_FEATURE )\n"
            b"event SomeModule::some_esp_message(c: connection, is_orig: bool,"
            b" msg: SomeModule::SomeEspMsg)"
            b" &group=MyPkg_SomeEvtGroup { shuntit(c); }\n"
            b"@endif\n"
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        # The continuation line (msg:) should start with tab+spaces, not all spaces
        msg_line = [l for l in lines if "msg:" in l]
        self.assertEqual(len(msg_line), 1)
        self.assertTrue(msg_line[0].startswith("\t"),
                        "Wrapped param line should start with tab inside @ifdef")

    def test_assignment_with_nested_function_alignment(self):
        """Assignment RHS with function call should align arguments correctly."""
        code = b"some_table_variable_name[some_key] = X509::Result($result=result$result, $result_string=result$result_string);"
        result = self._format(code).decode()
        # Should not have MISINDENTATION
        self.assertNotIn("MISINDENTATION", result)
        # Both $result and $result_string should be present (not split on $)
        self.assertIn("$result=result$result", result)
        self.assertIn("$result_string=result$result_string", result)

    def test_comments_count_toward_line_length(self):
        """Regular comments should count toward line length."""
        # This line is ~127 chars with the comment - should trigger breaking
        code = b"age_d = interval_to_double(network_time() - cert$not_valid_before) / 86400.0; # This is a comment that makes the line very long"
        result = self._format(code).decode()
        # Should have been broken into multiple lines
        self.assertGreater(len(result.splitlines()), 1)

    def test_annotation_comments_excluded_from_line_length(self):
        """Comments starting with #@ should not count toward line length."""
        # Same line but with #@ annotation - should NOT trigger breaking
        code = b"age_d = interval_to_double(network_time() - cert$not_valid_before) / 86400.0; #@ annotation"
        result = self._format(code).decode()
        # Should stay on one line (the code part fits, annotation excluded)
        lines = [l for l in result.splitlines() if l.strip()]
        self.assertEqual(len(lines), 1)

    def test_annotation_comment_preserves_alignment(self):
        """Continuation after annotation comment should align with arguments."""
        code = b'''other_function_abc(fmt("tracking %s %s", host, some_uid, #@ NOT-TESTED
            some_dest, extra_data));'''
        result = self._format(code).decode()
        lines = result.splitlines()
        # Find the line with fmt( and the continuation line
        fmt_line = None
        cont_line = None
        for i, line in enumerate(lines):
            if 'fmt(' in line:
                fmt_line = line
                if i + 1 < len(lines):
                    cont_line = lines[i + 1]
                break
        self.assertIsNotNone(fmt_line)
        self.assertIsNotNone(cont_line)
        # The continuation should align with the fmt arguments
        fmt_idx = fmt_line.index('fmt(')
        arg_start = fmt_idx + 4  # After "fmt("
        first_char_col = len(cont_line) - len(cont_line.lstrip())
        self.assertEqual(first_char_col, arg_start)

    def test_skip_pointless_line_breaks(self):
        """Don't break if continuation would be as long as original."""
        # Breaking at ( would shift content but not reduce max line length
        code = b'print fmt("This is a really long string argument that cannot be broken up into smaller pieces");'
        result = self._format(code).decode()
        # Should stay on one line - breaking wouldn't help
        lines = [l for l in result.splitlines() if l.strip()]
        self.assertEqual(len(lines), 1)

    def test_keep_fmt_string_together(self):
        """Keep fmt("string") together, break at comma after string."""
        code = b'local x = [$msg=fmt("Host uses protocol version %s which is lower than the safe minimum %s", some_string, other_string_a)];'
        result = self._format(code).decode()
        # Should NOT break at fmt( - instead break at comma after string
        # Line 1: ...fmt("...string...",
        # Line 2: some_string, other_string_a)...
        self.assertIn('fmt("Host uses protocol', result)
        self.assertNotIn('fmt(\n', result)
        self.assertNotIn('fmt(\t', result)

    def test_record_fields_combine_when_fit(self):
        """Record fields should combine on one line when they fit under 80 chars."""
        # This tests that newlines aren't counted in line length (off-by-one fix).
        # With indentation, the alignment column changes and the last two fields
        # fit exactly at 80 chars - but only if newlines are excluded from length.
        code = b'''function f() {
\tif ( x )
\t\tInput::add_event([$source=some_config_source_setting, $name=some_config_name_setting, $fields=SomeConfigRecord, $mode=Input::REREAD, $want_record=F, $ev=some_config_event_added]);
}'''
        result = self._format(code).decode()
        # $want_record=F and $ev should be on the same line since they fit at 80 chars
        self.assertIn('$want_record=F, $ev=some_config_event_added', result)

    def test_enum_single_line_preserved(self):
        """Enum on single line in source should stay on single line if it fits."""
        code = b'type score: enum { NOT = 0, LOW = 1, MED = 2, HIGH = 3 };'
        result = self._format(code).decode()
        # Should stay on one line
        lines = [l for l in result.splitlines() if l.strip()]
        self.assertEqual(len(lines), 1)
        self.assertIn('{ NOT = 0, LOW = 1, MED = 2, HIGH = 3 }', result)

    def test_enum_wrap_alignment(self):
        """Wrapped enum values should align to after '{ '."""
        code = b'type SomeEnumAA: enum { SE_DNS_A, SE_DNS_AAAA, SE_DNS_A6, SE_DNS_PTR, SE_HTTP_HOST, SE_TLS_SNI, SE_NTLM_AUTH, SE_UNKNOWN, };'
        result = self._format(code).decode()
        # Should not have MISINDENTATION marker
        self.assertNotIn("MISINDENTATION", result)
        # Verify continuation aligns to after '{ '
        lines = result.splitlines()
        self.assertGreater(len(lines), 1)
        # Find column of first enum value after '{ '
        line1 = lines[0]
        brace_idx = line1.index('{ ')
        first_val_col = brace_idx + 2  # after '{ '
        # Continuation should align to same column
        line2 = lines[1]
        first_char_col = len(line2) - len(line2.lstrip())
        self.assertEqual(first_char_col, first_val_col)

    def test_event_type_parameter_alignment(self):
        """Event type parameters should align to after the opening paren."""
        code = b'global some_event_decl: event(o: string, label: string, conf: score, sources: set[string], caller: string);'
        result = self._format(code).decode()
        lines = result.splitlines()
        # Should wrap and align continuation to after 'event('
        self.assertGreater(len(lines), 1)
        # Find the column where 'o:' starts on line 1
        line1 = lines[0]
        o_col = line1.index('o:')
        # Continuation should align to same column
        line2 = lines[1]
        # Find first non-space char in line2
        first_char_col = len(line2) - len(line2.lstrip())
        self.assertEqual(first_char_col, o_col)

    def test_event_statement_argument_alignment(self):
        """Event statement arguments should align to after the opening paren."""
        code = b"function some_func_a() { if ( foo ) event SomeModule::some_raised_evt(o, label, conf, source, caller); }"
        result = self._format(code).decode()
        # Should not have MISINDENTATION marker
        self.assertNotIn("MISINDENTATION", result)
        # Verify arguments wrap with proper alignment
        lines = result.splitlines()
        # Find line with event call and continuation
        event_line = None
        cont_line = None
        for i, line in enumerate(lines):
            if "event SomeModule::some_raised_evt" in line:
                event_line = line
                if i + 1 < len(lines):
                    cont_line = lines[i + 1]
                break
        self.assertIsNotNone(event_line)
        self.assertIsNotNone(cont_line)
        # Continuation should align to after '('
        paren_col = event_line.index("(") + 1
        first_char_col = len(cont_line) - len(cont_line.lstrip())
        self.assertEqual(first_char_col, paren_col)

    def test_in_and_not_in_operator_alignment(self):
        """The in and !in operators should align continuations like other binary operators."""
        # Test !in
        code1 = b'return a_long_var_name in subj && b_long_var_name !in subj && c_long_var_name !in subj;'
        result1 = self._format(code1).decode()
        self.assertNotIn("MISINDENTATION", result1)
        # Test in
        code2 = b'return some_very_long_variable_name in some_other_very_long_variable_name && another_thing in yet_another_thing;'
        result2 = self._format(code2).decode()
        self.assertNotIn("MISINDENTATION", result2)

    def test_print_statement_alignment(self):
        """Print statement arguments should align to after 'print '."""
        code = b'function f() { for ( a, b in tbl ) print file_handle, some_variable, another_table[some_index], final_value; }'
        result = self._format(code).decode()
        # Should not have MISINDENTATION marker
        self.assertNotIn("MISINDENTATION", result)
        # Verify continuation aligns to after 'print '
        lines = result.splitlines()
        print_line = None
        cont_line = None
        for i, line in enumerate(lines):
            if "print file_handle" in line:
                print_line = line
                if i + 1 < len(lines):
                    cont_line = lines[i + 1]
                break
        self.assertIsNotNone(print_line)
        self.assertIsNotNone(cont_line)
        # Continuation should align to after 'print '
        print_idx = print_line.index("print ")
        first_arg_col = print_idx + 6  # len("print ")
        first_char_col = len(cont_line) - len(cont_line.lstrip())
        self.assertEqual(first_char_col, first_arg_col)

    def test_schedule_expression_alignment(self):
        """Schedule expression should align continuation to after 'schedule '."""
        code = b'function f() { schedule some_time_interval { SomeModule::some_very_long_event_name() }; }'
        result = self._format(code).decode()
        # Should not have MISINDENTATION marker
        self.assertNotIn("MISINDENTATION", result)
        # Verify wrapping aligns properly
        lines = result.splitlines()
        schedule_line = None
        cont_line = None
        for i, line in enumerate(lines):
            if "schedule some_time_interval" in line and "{" not in line:
                schedule_line = line
                if i + 1 < len(lines):
                    cont_line = lines[i + 1]
                break
        if schedule_line and cont_line:
            # Continuation should align to after 'schedule '
            schedule_idx = schedule_line.index("schedule ")
            interval_col = schedule_idx + 9  # len("schedule ")
            first_char_col = len(cont_line) - len(cont_line.lstrip())
            self.assertEqual(first_char_col, interval_col)

    def test_case_label_alignment(self):
        """Case label values should align to after 'case '."""
        code = b'function f() { switch tag_type { case "some_analyzer_udp", "some_analyzer_udp_hmac_md5", "some_analyzer_udp_hmac_sha1", "some_analyzer_udp_hmac_sha256": break; } }'
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        # Verify continuation aligns to after 'case '
        lines = result.splitlines()
        for i, line in enumerate(lines):
            if 'case "some_analyzer_udp"' in line and i + 1 < len(lines):
                case_idx = line.index('case ')
                first_val_col = case_idx + 5  # len('case ')
                cont_line = lines[i + 1]
                first_char_col = len(cont_line) - len(cont_line.lstrip())
                self.assertEqual(first_char_col, first_val_col)
                break

    def test_local_declaration_alignment(self):
        """Local declaration initializer should align when wrapped."""
        code = b'function f() { local info = SomeModule::SomeRecord($field1=some_very_long_value, $field2=another_long_value); }'
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        # Verify wrapping occurs and arguments align after '('
        lines = result.splitlines()
        for i, line in enumerate(lines):
            if "SomeModule::SomeRecord(" in line and i + 1 < len(lines):
                paren_col = line.index("(") + 1
                cont_line = lines[i + 1]
                first_char_col = len(cont_line) - len(cont_line.lstrip())
                self.assertEqual(first_char_col, paren_col)
                break

    def test_local_declaration_rhs_spill_indent(self):
        """Local RHS spilled to next line should indent one past identifier alignment."""
        code = b'function some_func_bb(c: connection, cnt: count): interval { local some_long_variable_a = double_to_count(floor(c$orig$size / some_long_variable_bb)); }'
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        lines = result.splitlines()
        # Find the local declaration and its continuation
        for i, line in enumerate(lines):
            if "local some_long_variable_a =" in line and "double_to_count" not in line:
                # RHS spilled to next line
                cont_line = lines[i + 1]
                # The identifier starts at tab (8) + len("local ") = 14
                # Continuation should be at identifier_col + 1
                local_line = line
                local_idx = local_line.index("local ") + 6  # column of identifier
                id_col = local_idx  # visual column (after expanding tab)
                cont_col = len(cont_line) - len(cont_line.lstrip())
                self.assertEqual(cont_col, id_col + 1,
                    f"Expected indent one past identifier at col {id_col}, got {cont_col}")
                break

    def test_ternary_as_function_argument_alignment(self):
        """Ternary expressions as function arguments should preserve outer alignment."""
        code = b'function f() { if ( ! some_function_name(some_argument, new_file, extra_vals, some_uids, some_ids, info$source ? info$source : "", info$mime ? info$mime : "", info$md5 ? info$md5 : "") ) print "failed"; }'
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        # All arguments (including ternary ones) should align after '('
        lines = result.splitlines()
        for i, line in enumerate(lines):
            if "some_function_name(" in line:
                paren_col = line.index("some_function_name(") + len("some_function_name(")

                # Check that continuation lines align to paren_col
                for cont_line in lines[i + 1:]:
                    if cont_line.strip().startswith(": "):
                        # Ternary false branch - aligned by ternary logic, not function args
                        continue
                    if "print" in cont_line or cont_line.strip() == "}":
                        break
                    first_char_col = len(cont_line) - len(cont_line.lstrip())
                    self.assertEqual(first_char_col, paren_col,
                        f"Misaligned: {cont_line.strip()!r} at col {first_char_col}, expected {paren_col}")
                break

    def test_for_loop_alignment(self):
        """For loop content should align when wrapped."""
        code = b'function f() { for ( idx in result$matches ) for ( conn_idx in result$some_uids_with_a_very_long_field_name_that_forces_wrapping ) local x = 1; }'
        result = self._format(code).decode()
        # Should not have MISINDENTATION marker
        self.assertNotIn("MISINDENTATION", result)
        # Verify wrapping actually occurs on the inner for loop
        for i, line in enumerate(result.splitlines()):
            if "for ( conn_idx in" in line and ")" not in line:
                # Line wrapped - verify continuation aligns after '( '
                cont_line = result.splitlines()[i + 1]
                paren_col = line.index("( ") + 2
                first_char_col = len(cont_line) - len(cont_line.lstrip())
                self.assertEqual(first_char_col, paren_col)
                break

    def test_end_of_line_comment_stays_on_line(self):
        """End-of-line comments should stay with the statement even on long lines."""
        # This tests that deeply nested statements with end-of-line comments
        # don't get the comment pushed to a new line
        code = b'''function some_handler(c: connection)
    {
    if ( foo )
        {
        if ( bar )
            {
            if ( bletch )
                other_function_abc("Some kind of status message"); #@ NOT-TESTED
            }
        }
    }'''
        result = self._format(code).decode()
        # Should not have MISINDENTATION marker
        self.assertNotIn("MISINDENTATION", result)
        # The comment should be on the same line as the statement
        self.assertIn('message"); #@ NOT-TESTED', result)

    def test_boolean_op_preferred_over_arithmetic(self):
        """Line breaks should prefer && and || over arithmetic operators like -."""
        # This tests that when a line needs breaking, && is chosen over -
        code = b'''function some_function_name_aa(c: connection): string
    {
    if ( c?$tunnel && |c$tunnel| >= 2 &&
         c$tunnel[|c$tunnel| - 1]$tunnel_type == Tunnel::VXLAN )
        print "yep";
    }'''
        result = self._format(code).decode()
        # The break should be after && not inside the array index expression
        # Check that "- 1]" stays together (not split across lines)
        self.assertIn("- 1]", result)
        # Check that && is at end of a line (not start of next line)
        lines = result.splitlines()
        found_and_at_end = any(line.rstrip().endswith("&&") for line in lines)
        self.assertTrue(found_and_at_end, "Expected && at end of line")

    def test_attribute_keeps_equals_together(self):
        """Attributes like &default=expr should not break at the = sign."""
        code = b'global some_table: table[string] of count &default=some_long_default_expression_here &redef;'
        result = self._format(code).decode()
        # The &default=... should stay together
        self.assertIn("&default=some_long", result)
        # Should not have = at end of line or start of continuation
        lines = result.splitlines()
        for line in lines:
            stripped = line.rstrip()
            # = should not be at end of line (breaking after =)
            self.assertFalse(stripped.endswith("="), f"Line should not end with =: {line}")

    def test_compound_conditional_breaks_at_boolean_op(self):
        """Long compound conditionals should break at && or || operators."""
        code = b'if ( first_condition && second_condition && third_condition && some_object$long_field > another_object$other_field ) do_something();'
        result = self._format(code).decode()
        lines = result.splitlines()
        # Should have a line ending with &&
        found_and_at_end = any(line.rstrip().endswith("&&") for line in lines)
        self.assertTrue(found_and_at_end, "Expected && at end of line when breaking")
        # The > comparison should stay together, not be used as break point
        self.assertIn("long_field >", result)

    def test_arithmetic_stays_together_in_arguments(self):
        """Arithmetic expressions should not break when commas are available.

        When a function call has arguments that include arithmetic, the formatter
        should prefer breaking at commas rather than inside the arithmetic.
        """
        code = b'''NOTICE([$msg=fmt("Value %s was more than %dMB for item %s",
                some_var, some_long_variable_name / 1000 / 1000, bucket)]);'''
        result = self._format(code).decode()
        # Should NOT have a line ending with just / (breaking at arithmetic op)
        lines = result.splitlines()
        bad_break = any(line.rstrip().endswith("/") for line in lines)
        self.assertFalse(bad_break, "Should not break at / when commas available")
        # The threshold and its division should be on the same line
        self.assertTrue(
            any("some_long_variable_name" in line and "1000" in line for line in lines),
            "Arithmetic expression should stay together on one line"
        )

    def test_no_orphan_arithmetic_operator(self):
        """Arithmetic operators should not be alone on a line."""
        code = b'function some_func_bb(c: connection, cnt: count): interval { if ( some_long_variable_a > 0 ) { SomeModule::some_long_function_name(c, (some_long_variable_a + 3) * some_long_variable_bb, F); } }'
        result = self._format(code).decode()
        lines = result.splitlines()
        # The * should be at end of a line, not orphaned on its own line
        orphan_star = any(line.strip() == "*" for line in lines)
        self.assertFalse(orphan_star, "* operator should not be alone on a line")
        # * should be at end of some line
        found_star_at_end = any(line.rstrip().endswith("*") for line in lines)
        self.assertTrue(found_star_at_end, "Expected * at end of line")

    def test_arithmetic_operator_at_end_when_breaking(self):
        """When arithmetic must break, keep operator at end of line.

        If no commas are available and arithmetic must break, the operator
        should stay at the end of line 1, not the start of line 2.
        """
        code = b'''if ( age >= 1 day )
            age_d = interval_to_double(network_time() -
                                       cert$not_valid_before) /
                    86400.0;'''
        result = self._format(code).decode()
        lines = result.splitlines()
        # Find the line with the / operator - it should END with /
        found_div_at_end = any(line.rstrip().endswith("/") for line in lines)
        self.assertTrue(found_div_at_end, "Division operator should be at end of line")
        # 86400.0 should be on its own continuation line, not starting with /
        found_div_at_start = any(line.lstrip().startswith("/") for line in lines)
        self.assertFalse(found_div_at_start, "Division operator should not start a line")

    def test_ternary_break_after_question_mark(self):
        """Ternary expressions should break after ? with 8-space indent."""
        code = b'local x = some_very_long_condition_expression_here ? some_very_long_true_value_expression_here : some_long_false;'
        result = self._format(code).decode()
        lines = result.splitlines()
        # Should have a line ending with ?
        found_question_at_end = any(line.rstrip().endswith("?") for line in lines)
        self.assertTrue(found_question_at_end, "Expected ? at end of line when breaking")
        # Second line should be indented (8 spaces = one tab)
        if len(lines) > 1:
            self.assertTrue(lines[1].startswith("\t") or lines[1].startswith("        "),
                           "Expected 8-space indent after ?")

    def test_ternary_break_after_colon(self):
        """Ternary expressions should break after : with alignment to true expr."""
        code = b'local x = cond ? some_very_very_very_long_true_value_expression_here : some_very_very_very_long_false_value_expression_here;'
        result = self._format(code).decode()
        lines = result.splitlines()
        # Should have a line ending with :
        found_colon_at_end = any(line.rstrip().endswith(":") for line in lines)
        self.assertTrue(found_colon_at_end, "Expected : at end of line when breaking")
        # Find the column of the true expression and verify false expr aligns
        for i, line in enumerate(lines):
            if "? " in line and line.rstrip().endswith(":"):
                true_col = line.index("? ") + 2  # After "? "
                if i + 1 < len(lines):
                    false_line = lines[i + 1]
                    false_col = len(false_line) - len(false_line.lstrip())
                    self.assertEqual(false_col, true_col,
                                   f"False expr should align with true expr at col {true_col}")
                break

    def test_ternary_prefers_break_after_colon_not_question(self):
        """Ternary should prefer breaking after : over breaking after ?.

        When a ternary fits on one line except for the false expression,
        we should break after : (keeping ? and true expr together), not
        break after ? (which would waste space).
        """
        code = b'''event some_event_abc(host: addr, svc: string, region: string)
    {
    local endpoints = some_long_variable_ab > 0 ? calls$some_fld :
                                                  "Disabled";
    }
'''
        result = self._format(code).decode()
        lines = result.splitlines()
        # Find the ternary line - should end with : not ?
        ternary_line = None
        for line in lines:
            if "?" in line and ":" in line:
                ternary_line = line
                break
        self.assertIsNotNone(ternary_line, "Expected to find ternary line")
        # The line should end with : (break after :, not after ?)
        self.assertTrue(ternary_line.rstrip().endswith(":"),
                       f"Expected ternary line to end with ':', got: {ternary_line}")
        # Should NOT have a line ending with just ?
        question_only_lines = [l for l in lines if l.rstrip().endswith("?")]
        self.assertEqual(len(question_only_lines), 0,
                        "Should not break after ? when : break suffices")

    def test_type_declaration_keeps_colon(self):
        """Type declarations should NOT break after the colon.

        The colon in 'var: type' is different from ternary ':' and should
        not be treated as a line break point.
        """
        code = b'''global some_table_name_a: table[count, string] of TrackingRec
        &default_insert = TrackingRec() &create_expire = 10 sec;'''
        result = self._format(code).decode()
        lines = result.splitlines()
        # First line should contain "some_table_name_a:" followed by table type
        first_line = lines[0]
        self.assertIn("some_table_name_a:", first_line)
        self.assertIn("table[", first_line)
        # Should NOT have a line that's just "some_table_name_a:" followed by newline
        bad_break = any(line.rstrip().endswith("some_table_name_a:") for line in lines)
        self.assertFalse(bad_break, "Type declaration should not break after colon")

    def test_nested_function_call_alignment(self):
        """Nested function calls should align correctly when outer call wraps."""
        code = b'local filter = Log::Filter($name="log-name", $path="log_path", $include=set("id.orig_h", "id.orig_p", "id.resp_h", "id.resp_p", "app"), $policy=some_policy_fn);'
        result = self._format(code).decode()
        # Should not have MISINDENTATION marker
        self.assertNotIn("MISINDENTATION", result)
        lines = result.splitlines()
        # Find lines with Log::Filter( and set( continuations
        outer_line = None
        set_line = None
        set_cont_line = None
        for i, line in enumerate(lines):
            if "Log::Filter(" in line:
                outer_line = line
            if "$include=set(" in line:
                set_line = line
                if i + 1 < len(lines):
                    set_cont_line = lines[i + 1]
        self.assertIsNotNone(outer_line)
        self.assertIsNotNone(set_line)
        self.assertIsNotNone(set_cont_line)
        # Verify nested set() continuation aligns to after set(
        set_paren_col = set_line.index("set(") + 4
        set_cont_col = len(set_cont_line) - len(set_cont_line.lstrip())
        self.assertEqual(set_cont_col, set_paren_col)

    def test_constructor_formatting_ignores_input_newlines(self):
        """Constructor calls should format the same regardless of input whitespace."""
        # Single-line input
        code1 = b'local x = set("a", "b", "c", "d", "e");'
        # Multi-line input (same semantically)
        code2 = b'local x = set("a",\n"b",\n"c",\n"d",\n"e");'

        result1 = self._format(code1)
        result2 = self._format(code2)
        # Both should produce identical output
        self.assertEqual(result1, result2)

    def test_large_constructor_uses_one_per_line(self):
        """Constructors >80 chars should use one-per-line format."""
        # Long set (>80 chars) - should be one-per-line regardless of input
        code1 = b'local x = set(1.0.0.2/31, 1.1.1.2/31, 3.7.176.123/32, 5.1.66.255/32, 5.2.75.75/32, 5.45.107.88/32, 8.8.4.4/32);'
        code2 = b'local x = set(1.0.0.2/31,\n1.1.1.2/31,\n3.7.176.123/32,\n5.1.66.255/32,\n5.2.75.75/32,\n5.45.107.88/32,\n8.8.4.4/32);'

        result1 = self._format(code1).decode()
        result2 = self._format(code2).decode()

        # Both should produce identical output
        self.assertEqual(result1, result2)
        # Should have multiple lines (one-per-line format)
        self.assertGreater(len(result1.splitlines()), 1)
        # Each element should be on its own line
        self.assertIn("1.0.0.2/31,\n", result1)

    def test_multiline_element_aligns_next_element(self):
        """When an element spans multiple lines, next element starts on new aligned line."""
        code = b'local filter = Log::Filter($name="log-name", $path="log_path", $include=set("id.orig_h", "id.orig_p", "id.resp_h", "id.resp_p", "app"), $policy=some_policy_fn);'
        result = self._format(code).decode()
        lines = result.splitlines()
        # Find lines with $include and $policy
        include_line = None
        policy_line = None
        for i, line in enumerate(lines):
            if "$include=" in line:
                include_line = i
            if "$policy=" in line:
                policy_line = i
        self.assertIsNotNone(include_line)
        self.assertIsNotNone(policy_line)
        # $policy should be on a different line than where $include starts
        self.assertGreater(policy_line, include_line)
        # $policy should be aligned with $include (same indentation)
        include_indent = len(lines[include_line]) - len(lines[include_line].lstrip())
        policy_indent = len(lines[policy_line]) - len(lines[policy_line].lstrip())
        self.assertEqual(policy_indent, include_indent)

    def test_multiline_element_aligns_next_element_indented(self):
        """Same as above but inside an indented block - indentation shouldn't change behavior."""
        code = b'event zeek_init() { local filter = Log::Filter($name="log-name", $path="log_path", $include=set("id.orig_h", "id.orig_p", "id.resp_h", "id.resp_p", "app"), $policy=some_policy_fn); }'
        result = self._format(code).decode()
        lines = result.splitlines()
        # Find lines with $include and $policy
        include_line = None
        policy_line = None
        for i, line in enumerate(lines):
            if "$include=" in line:
                include_line = i
            if "$policy=" in line:
                policy_line = i
        self.assertIsNotNone(include_line)
        self.assertIsNotNone(policy_line)
        # $policy should be on a different line than where $include starts
        self.assertGreater(policy_line, include_line)
        # $policy should be aligned with $include (same indentation)
        include_indent = len(lines[include_line]) - len(lines[include_line].lstrip())
        policy_indent = len(lines[policy_line]) - len(lines[policy_line].lstrip())
        self.assertEqual(policy_indent, include_indent)

    def test_standalone_comment_after_function(self):
        """Comments on their own line after a function should not be indented."""
        code = b'''#@ BEGIN
function foo()
    {
    bar();
    }
#@ END
'''
        result = self._format(code).decode()
        lines = result.splitlines()
        # Find the #@ END comment
        end_line = None
        for line in lines:
            if "#@ END" in line:
                end_line = line
                break
        self.assertIsNotNone(end_line)
        # Should be at column 0 (no indentation)
        self.assertFalse(end_line.startswith("\t"))
        self.assertFalse(end_line.startswith(" "))

    def test_record_args_alignment_ignores_source_newlines(self):
        """Record-style arguments should format consistently regardless of source whitespace."""
        # Single-line input
        code1 = b'local filter = Log::Filter($name="log-name", $path="log_path", $include=set("id.orig_h", "id.orig_p", "id.resp_h", "id.resp_p", "app"), $policy=some_policy_fn);'
        # Multi-line input (same semantically)
        code2 = b'''local filter = Log::Filter($name="log-name", $path="log_path",
                               $include=set("id.orig_h", "id.orig_p",
                                            "id.resp_h", "id.resp_p",
                                            "app"), $policy=some_policy_fn);'''

        result1 = self._format(code1)
        result2 = self._format(code2)
        # Both should produce identical output
        self.assertEqual(result1, result2)
        # $policy should be on its own line (not after closing paren of set)
        decoded = result1.decode()
        self.assertIn("),\n", decoded)  # Newline after set's closing paren
        self.assertIn("$policy=", decoded)

    def test_call_with_comment_aligns_args(self):
        """Arguments after a comment following '(' should align to the column after '('."""
        code = b'''function foo()
\t{
\tcall( # with a comment
\t    arg1, arg2);
\t}
'''
        result = self._format(code)
        lines = result.decode().split("\n")
        # Find the args line
        args_line = None
        for line in lines:
            if "arg1" in line:
                args_line = line
                break
        self.assertIsNotNone(args_line)
        # Should be aligned: \t (8) + 5 spaces = column 13, right after "call("
        # The line should have tab + spaces to align with column after '('
        self.assertTrue(args_line.startswith("\t     "), f"Got: {repr(args_line)}")

    def test_string_concat_alignment(self):
        """String concatenation continuations should align to the first string."""
        # Use long strings that will force line wrapping
        code = b'''function foo()
\t{
\tprint "Lovely patio around the fountain. " +
\t    "Spent a lovely lunch on the patio. " +
\t    "The menu was inviting and lots of things I wanted to order.";
\t}
'''
        result = self._format(code)
        lines = result.decode().split("\n")
        # Find lines with strings (but not the first print line)
        str_lines = [l for l in lines if '"' in l and "print" not in l]
        # Should have continuation lines
        self.assertGreater(len(str_lines), 0, f"No continuation lines found. Output:\n{result.decode()}")
        # All continuation lines should be aligned (same indentation)
        # Check that they start with tab + spaces (proper alignment, no MISINDENTATION)
        for line in str_lines:
            self.assertNotIn("MISINDENTATION", line, f"Got MISINDENTATION in: {repr(line)}")
            self.assertTrue(line.startswith("\t"), f"Expected tab indent, got: {repr(line)}")

    def test_preproc_if_indents_at_top_level(self):
        """Content inside @if blocks at top level should be indented."""
        code = b'''@if ( FOO )
print 1;
@endif
'''
        result = self._format(code)
        lines = result.decode().split("\n")
        # Find the print line
        print_line = [l for l in lines if "print" in l][0]
        # Should be indented with one tab
        self.assertTrue(print_line.startswith("\t"), f"Expected tab indent, got: {repr(print_line)}")

    def test_preproc_if_no_extra_indent_in_function(self):
        """Content inside @if blocks within functions should not get extra indent."""
        code = b'''function foo()
\t{
@if ( FOO )
\tprint 1;
@endif
\t}
'''
        result = self._format(code)
        lines = result.decode().split("\n")
        # Find the print line
        print_line = [l for l in lines if "print" in l][0]
        # Should have exactly one tab (function body indent), not two
        self.assertEqual(print_line, "\tprint 1;", f"Got: {repr(print_line)}")

    def test_preproc_if_else_with_load(self):
        """@load and other directives inside @if/@else should be indented."""
        code = b'''@if ( FOO )
@load foo
@else
@load bar
@endif
'''
        result = self._format(code)
        lines = result.decode().split("\n")
        # @if, @else, @endif should be at column 0
        self.assertTrue(lines[0].startswith("@if"))
        self.assertEqual(lines[2], "@else")
        self.assertEqual(lines[4], "@endif")
        # @load lines should be indented
        self.assertEqual(lines[1], "\t@load foo")
        self.assertEqual(lines[3], "\t@load bar")

    def test_record_field_attr_list_no_misindent(self):
        """Record field attr_list should not produce MISINDENTATION on line break."""
        # This line is long enough to require breaking when deeply indented
        code = b'''export {
\ttype SomeAttrs: record {
\t\tsome_strings: set[string] &log &optional; # A set of associated strings
\t};
}
'''
        result = self._format(code)
        self.assertNotIn(b"MISINDENTATION", result)
        # The output should still be valid (no MISINDENTATION markers)
        self.assertIn(b"&optional", result)

    def test_switch_paren_expr_has_spaces(self):
        """Parenthesized switch expressions should have spaces inside parens."""
        code = b'function f() { switch (val) { case 0: break; } }'
        result = self._format(code).decode()
        self.assertIn("switch ( val )", result)

    def test_switch_bare_expr_no_spaces(self):
        """Non-parenthesized switch expressions should remain unchanged."""
        code = b'function f() { switch val { case 0: break; } }'
        result = self._format(code).decode()
        self.assertIn("switch val", result)
        self.assertNotIn("switch ( val )", result)

    def test_switch_brace_on_same_line_and_case_not_indented(self):
        """Switch opening brace on same line, cases/closing brace at switch indent."""
        code = b'function f() { switch (val) { case 0: break; case 1: break; } }'
        result = self._format(code).decode()
        lines = result.splitlines()
        switch_line = next(l for l in lines if "switch" in l)
        # Opening brace on same line as switch
        self.assertTrue(switch_line.rstrip().endswith("{"))
        # Find indent of the switch line
        switch_indent = len(switch_line) - len(switch_line.lstrip())
        # Case lines should be at same indent level as switch
        case_lines = [l for l in lines if l.lstrip().startswith("case ")]
        for cl in case_lines:
            case_indent = len(cl) - len(cl.lstrip())
            self.assertEqual(case_indent, switch_indent)
        # Closing brace at same indent level as switch
        close_brace = next(l for l in lines if l.strip() == "}")
        brace_indent = len(close_brace) - len(close_brace.lstrip())
        self.assertEqual(brace_indent, switch_indent)

    def test_comma_before_bracket_is_break_point(self):
        """Comma before [...] initializer should be a valid line break point."""
        code = b'event zeek_init() { SomeModule::some_register_fn(SomeModule::SOME_ANALYZER, [$get_handle=SomeModule::get_handle, $describe=SomeModule::describe_it]); }'
        result = self._format(code).decode()
        lines = result.splitlines()
        # The comma should break the line, not end up at start of continuation
        call_line = next(l for l in lines if "some_register_fn" in l)
        self.assertTrue(call_line.rstrip().endswith(","))
        # The continuation should start with [
        next_idx = lines.index(call_line) + 1
        self.assertTrue(lines[next_idx].lstrip().startswith("["))

    def test_export_trailing_comments_indented(self):
        """Comments at end of export block should be indented like declarations."""
        code = b'export {\n\tglobal some_evt: event(rec: Info);\n\n\t## Some trailing comment\n\t## Another trailing comment\n}\n'
        result = self._format(code).decode()
        lines = result.splitlines()
        for line in lines:
            if line.lstrip().startswith("##"):
                self.assertTrue(line.startswith("\t"),
                                f"Comment not indented: {line!r}")


class TestFormattingErrors(unittest.TestCase):
    def _format(self, content):
        script = zeekscript.Script(io.BytesIO(content))

        # Everything passed in has a syntax error, so parsing should always
        # fail, and has_error() should be True.
        self.assertFalse(script.parse())
        self.assertTrue(script.has_error())

        buf = io.BytesIO()
        script.format(buf)

        return buf.getvalue(), script.get_error()

    def assertFormatting(self, input_, baseline, error_baseline):
        # Verify formatting and reported error
        result1, error1 = self._format(tu.normalize(input_))
        self.assertEqual(tu.normalize(baseline), result1)
        self.assertEqual(error_baseline, error1)

        # Format again. There should be no change to the formatting.
        result2, _ = self._format(result1)
        self.assertEqual(tu.normalize(baseline), result2)

    def test_start_error(self):
        self.assertFormatting(
            "xxx  function foo() { }",
            """xxx function foo()
	{ }
""",
            ("xxx  function foo() { }", 0, 'cannot parse line 0, col 0: "xxx"'),
        )

    def test_mid_error(self):
        self.assertFormatting(
            """module Foo;

function foo) { print  "hi" ; }
""",
            """module Foo;

function foo) {
print "hi";
}
""",
            (
                'function foo) { print  "hi" ; }',
                2,
                'cannot parse line 2, col 0: "function foo)"',
            ),
        )

    def test_mid_record_error(self):
        _, error = self._format(
            tu.normalize(
                """type foo: record {
	a: count; ##< A field
	b count; ##< A broken field
	c: count; ##< Another field, better not skipped!
	d: count; ##< Ditto.
};
"""
            )
        )

        # TODO(bbannier): The way we currently format this is not idempotent,
        # so only check that we return the expected error. This is okay since
        # we do not format files with errors anyway.
        assert error == (
            "type foo: record {",
            0,
            'cannot parse line 0, col 10: "record {\n\ta: count; ##< A field\n\tb"',
        )

    def test_single_char_mid_error(self):
        # In this example, the error node spans just a single character:
        self.assertFormatting(
            "event zeek_init() { foo)(); }",
            """event zeek_init()
	{
	foo ) ();
	}
""",
            ("event zeek_init() { foo)(); }", 0, 'cannot parse line 0, col 23: ")"'),
        )

    def test_tail_error_no_nl(self):
        self.assertFormatting(
            "function foo( )  { if (",
            "function foo()  { if (\n",
            (
                "function foo( )  { if (",
                0,
                'cannot parse line 0, col 0: "function foo( )  { if ("',
            ),
        )

    def test_tail_error_nl(self):
        self.assertFormatting(
            "function foo( ) { if (\n",
            "function foo() { if (\n",
            (
                "function foo( ) { if (",
                0,
                'cannot parse line 0, col 0: "function foo( ) { if ("',
            ),
        )


class TestNewlineFormatting(unittest.TestCase):
    # This test verifies correct processing when line endings in the input
    # differ from that normally used by the platform.

    def test_file_formatting(self):
        given = tu.SAMPLE_UNFORMATTED.encode("utf-8")
        # Swap line endings for something not native to the platform:
        if zeekscript.Formatter.NL == b"\n":
            # Turn everything to \r\n, even if mixed
            given = given.replace(b"\r\n", b"\n")
            given = given.replace(b"\n", b"\r\n")
        else:
            given = given.replace(b"\r\n", b"\n")

        buf = io.BytesIO(given)
        script = zeekscript.Script(buf)
        script.parse()

        buf = io.BytesIO()
        script.format(buf)

        given = buf.getvalue()

        expected = tu.SAMPLE_FORMATTED.encode("utf-8")
        self.assertEqual(expected, given)


class TestScriptConstruction(unittest.TestCase):
    DATA = "event zeek_init() { }"
    TMPFILE = "tmp.zeek"

    def test_file(self):
        # tempfile.NamedTemporaryFile doesn't seem to support reading while
        # still existing on some platforms, so going manual here:
        try:
            with open(self.TMPFILE, "w", encoding="utf-8") as hdl:
                hdl.write(self.DATA)
            script = zeekscript.Script(self.TMPFILE)
            script.parse()
        finally:
            os.unlink(self.TMPFILE)

    def test_path(self):
        try:
            with open(self.TMPFILE, "w", encoding="utf-8") as hdl:
                hdl.write(self.DATA)
            script = zeekscript.Script(pathlib.Path(hdl.name))
            script.parse()
        finally:
            os.unlink(self.TMPFILE)

    def test_stdin(self):
        oldstdin = sys.stdin

        try:
            sys.stdin = io.StringIO(self.DATA)
            script = zeekscript.Script("-")
            script.parse()
        finally:
            sys.stdin = oldstdin

    def test_text_fileobj(self):
        obj = io.StringIO(self.DATA)
        script = zeekscript.Script(obj)
        script.parse()

    def test_bytes_fileobj(self):
        obj = io.BytesIO(self.DATA.encode("UTF-8"))
        script = zeekscript.Script(obj)
        script.parse()
