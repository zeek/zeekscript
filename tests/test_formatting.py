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
        """Atomic RHS breaks after += with tab-indented continuation."""
        code = b"another_very_long_access_to_some_member += yetanotherveryveryveryverylongthing[0];"
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        lines = result.strip().split("\n")
        self.assertEqual(len(lines), 2, "should break into two lines")
        self.assertIn("+=", lines[0])
        self.assertTrue(lines[1].startswith("\t"))

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

    def test_enum_one_per_line_when_long(self):
        """Enum constants go one-per-line when they don't fit on a single line."""
        code = b'type SomeEnumAA: enum { SE_DNS_A, SE_DNS_AAAA, SE_DNS_A6, SE_DNS_PTR, SE_HTTP_HOST, SE_TLS_SNI, SE_NTLM_AUTH, SE_UNKNOWN, };'
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        lines = result.splitlines()
        # First line: "type SomeEnumAA: enum {"
        self.assertIn('enum {', lines[0])
        # Each constant on its own tab-indented line
        for const in ['SE_DNS_A,', 'SE_DNS_AAAA,', 'SE_DNS_A6,', 'SE_DNS_PTR,',
                      'SE_HTTP_HOST,', 'SE_TLS_SNI,', 'SE_NTLM_AUTH,', 'SE_UNKNOWN,']:
            self.assertTrue(any(l.strip() == const for l in lines),
                            f"{const} should be on its own line")

    def test_enum_one_per_line_in_export(self):
        """Enum in export block goes one-per-line when constants don't fit on one line."""
        code = (
            b'export {\n'
            b'type AnomalyTypes: enum {\n'
            b'\tANOMALY, NEW_ENTITY, NEW_ITEM, NEW_ENTITY_NEW_ITEM,\n'
            b'\tNEW_ENTITY_ITEM_PAIR, UNKNOWN\n'
            b'};\n'
            b'}\n'
        )
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        lines = result.splitlines()
        # Each constant should be on its own line, tab-indented inside export
        for const in ['ANOMALY,', 'NEW_ENTITY,', 'NEW_ITEM,',
                      'NEW_ENTITY_NEW_ITEM,', 'NEW_ENTITY_ITEM_PAIR,', 'UNKNOWN']:
            self.assertTrue(any(const in l for l in lines),
                            f"{const} should appear on its own line")
        # Constants should NOT be compacted onto fewer lines
        const_lines = [l for l in lines if any(c in l for c in ['ANOMALY', 'NEW_ENTITY', 'UNKNOWN'])]
        # Each constant line should have at most one constant
        for l in const_lines:
            consts_on_line = sum(1 for c in ['ANOMALY', 'NEW_ENTITY', 'NEW_ITEM',
                                              'NEW_ENTITY_NEW_ITEM', 'NEW_ENTITY_ITEM_PAIR',
                                              'UNKNOWN'] if c in l)
            # NEW_ENTITY_NEW_ITEM contains NEW_ENTITY and NEW_ITEM, so skip those
            if 'NEW_ENTITY_NEW_ITEM' in l or 'NEW_ENTITY_ITEM_PAIR' in l:
                continue
            self.assertLessEqual(consts_on_line, 1,
                                 f"Multiple constants on one line: {l}")

    def test_event_type_parameter_alignment(self):
        """Event type parameters should align to after the opening paren."""
        code = b'global some_event_decl: event(o: string, label: string, conf: score, sources: set[string], caller: string);'
        result = self._format(code).decode()
        lines = result.splitlines()
        # Should wrap — line is too long for 80 columns
        self.assertGreater(len(lines), 1)
        # Find the line containing 'event(' — may be line 0 or later (colon-break)
        event_line_idx = next(i for i, l in enumerate(lines) if '(' in l)
        expanded_event = lines[event_line_idx].replace('\t', '        ')
        paren_col = expanded_event.index('(') + 1
        for line in lines[event_line_idx + 1:]:
            if line.strip() and not line.strip().startswith(')'):
                expanded = line.replace('\t', '        ')
                first_char_col = len(expanded) - len(expanded.lstrip())
                self.assertEqual(first_char_col, paren_col,
                    f"Expected col {paren_col}, got {first_char_col}: {line!r}")

    def test_event_type_breaks_when_deep(self):
        """Event type in a global decl should break when line exceeds 80 cols."""
        code = (
            b'export {\n'
            b'\tglobal SomeModule::some_long_handler: event(anomaly_data: SomePredictions);\n'
            b'}\n'
        )
        result = self._format(code).decode()
        # Line should break — the full line would be > 80
        lines = result.splitlines()
        event_line = [l for l in lines if 'event(' in l][0]
        expanded = event_line.replace('\t', '        ')
        self.assertLessEqual(len(expanded), 80,
            f"Event type line too long ({len(expanded)}): {event_line!r}")

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

    def test_local_declaration_trailing_comment_breaks_at_equals(self):
        """Local with trailing comment that would overflow breaks at '=' instead of args."""
        code = (
            b'function some_func()\n'
            b'    {\n'
            b'    if ( test )\n'
            b'        {\n'
            b'        local some_val =\n'
            b'               some_long_func_name(result_chain[1], 4); # HASH\n'
            b'        }\n'
            b'    }\n'
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        # Should break at '=' — call + comment on continuation line
        eq_line = [l for l in lines if 'some_val =' in l][0]
        self.assertTrue(eq_line.rstrip().endswith('='),
            f"Expected '=' at end of line, got: {eq_line!r}")
        call_line = [l for l in lines if 'some_long_func_name(' in l][0]
        # Args should stay on one line (not broken across lines)
        self.assertIn(', 4); # HASH', call_line)

    def test_const_with_attr_breaks_at_equals(self):
        """Const with &redef attr that would overflow should break at '='."""
        code = (
            b'export {\n'
            b'    const cert_hygiene_server_wl_source =\n'
            b'           "cert-hygiene-server-wl.txt" &redef;\n'
            b'}\n'
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        eq_line = [l for l in lines if 'cert_hygiene_server_wl_source' in l][0]
        self.assertTrue(eq_line.rstrip().endswith('='),
            f"Expected '=' at end of line, got: {eq_line!r}")
        val_line = [l for l in lines if 'cert-hygiene-server-wl' in l][0]
        self.assertIn('&redef;', val_line,
            "Attr should be on same line as value after '=' break")

    def test_typed_const_with_attr_breaks_at_equals(self):
        """Typed const with &redef attr that would overflow should break at '='."""
        code = (
            b'export {\n'
            b'    const default_analyzer: PacketAnalyzer::Tag ='
            b' PacketAnalyzer::ANALYZER_IP &redef;\n'
            b'}\n'
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        eq_line = [l for l in lines if 'default_analyzer' in l][0]
        self.assertTrue(eq_line.rstrip().endswith('='),
            f"Expected '=' at end of line, got: {eq_line!r}")
        val_line = [l for l in lines if 'ANALYZER_IP' in l][0]
        self.assertIn('&redef;', val_line,
            "Attr should be on same line as value after '=' break")

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

    def test_type_declaration_colon_break(self):
        """Type declarations break after colon when type+attrs overflow."""
        code = b'''global some_table_name_a: table[count, string] of TrackingRec
        &default_insert = TrackingRec() &create_expire = 10 sec;'''
        result = self._format(code).decode()
        lines = result.splitlines()
        # Colon-break: identifier on line 0, type on indented continuation
        self.assertTrue(lines[0].rstrip().endswith(":"))
        self.assertIn("table[", result)
        # All lines under 80 columns
        for line in lines:
            expanded = line.replace('\t', '        ')
            self.assertLessEqual(len(expanded), 80, f"Line too long: {line!r}")

    def test_type_attrs_together_on_indented_line(self):
        """Type + attrs stay together on one indented line after colon-break."""
        code = (
            b'global ftp_connections_cache:'
            b' set[string, string, string]'
            b' &read_expire=cache_interval;\n'
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        # Colon-break: identifier on line 0
        self.assertTrue(lines[0].rstrip().endswith(":"))
        # Type and attr together on the indented continuation line
        type_attr_line = [l for l in lines if "set[" in l][0]
        self.assertIn("&read_expire", type_attr_line,
                       "Type and attr should be on the same line")

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

    def test_preproc_ifdef_indents_multiple_items(self):
        """Multiple items inside @ifdef at top level should all be indented."""
        code = (
            b'@ifdef ( SomeFeature )\n'
            b'\n'
            b'    module SomeMod;\n'
            b'\n'
            b'    const some_var = "some-value";\n'
            b'\n'
            b'    event zeek_init()\n'
            b'        {\n'
            b'        print some_var;\n'
            b'        }\n'
            b'@endif\n'
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        # All non-preproc, non-blank lines should be indented
        for line in lines:
            if line and not line.startswith('@') and line.strip():
                self.assertTrue(line.startswith('\t'),
                    f"Expected tab indent, got: {line!r}")
        # Should have exactly one blank line between items (not doubled)
        text = result.strip()
        self.assertNotIn('\n\n\n', text,
            "Should not have double blank lines")

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

    def test_comment_before_endif_indented(self):
        """Comments before @endif should be indented at the inner preproc depth."""
        code = (
            b'@ifdef ( SOME_FEATURE )\n'
            b'\n'
            b'    event zeek_init()\n'
            b'        {\n'
            b'        print some_var;\n'
            b'        }\n'
            b'\n'
            b'    # Note: This comment belongs inside the ifdef block.\n'
            b'    # It should be tab-indented.\n'
            b'@endif\n'
        )
        result = self._format(code).decode()
        self.assertIn('\t# Note: This comment belongs', result)
        self.assertIn('\t# It should be tab-indented.', result)
        # @endif itself should be at column 0
        self.assertIn('\n@endif\n', result)

    def test_comment_before_else_indented_in_preproc(self):
        """Comments before @else should be indented at the inner preproc depth."""
        code = (
            b'@ifdef ( SOME_FEATURE )\n'
            b'\n'
            b'    event zeek_init()\n'
            b'        {\n'
            b'        print some_var;\n'
            b'        }\n'
            b'\n'
            b'    # Fallback path below.\n'
            b'@else\n'
            b'\n'
            b'    event zeek_init()\n'
            b'        {\n'
            b'        print "other";\n'
            b'        }\n'
            b'\n'
            b'@endif\n'
        )
        result = self._format(code).decode()
        self.assertIn('\t# Fallback path below.', result)
        self.assertIn('\n@else\n', result)

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

    def test_global_set_type_wraps_attr_when_long(self):
        """set[...] type params flow across lines; attr wraps after colon-break."""
        code = (
            b'global some_long_cache_name:'
            b' set[string, subnet, subnet, transport_proto, SomeModule::SomeType]'
            b' &read_expire=cache_interval;\n'
        )
        result = self._format(code).decode()
        lines = result.rstrip().split('\n')
        # All lines under 80 columns
        for line in lines:
            self.assertLessEqual(len(line), 80, f"Line too long: {repr(line)}")
        # Type params flow (not one-per-line): type line has multiple types
        self.assertIn("set[string, subnet, subnet, transport_proto,", result)
        # attr_list wraps to its own line
        attr_line = [l for l in lines if "&read_expire" in l][0]
        self.assertNotIn("]", attr_line, "attr should be on its own line, not after ]")
        # After colon-break, type and attr align at same tab indent
        set_line = [l for l in lines if "set[" in l][0]
        set_col = len(set_line.replace('\t', '        ')) - len(set_line.replace('\t', '        ').lstrip())
        attr_col = len(attr_line.replace('\t', '        ')) - len(attr_line.replace('\t', '        ').lstrip())
        self.assertEqual(attr_col, set_col,
                         f"Expected attr at col {set_col}, got {attr_col}")

    def test_multiple_attrs_wrap_and_align(self):
        """Multiple attrs wrap one-per-line, all aligned to same column."""
        code = (
            b"global some_tracking_var: table[count, string] of TrackingRec\n"
            b"\t&default_insert = TrackingRec($field=val)"
            b" &create_expire = 10 sec &redef;\n"
        )
        result = self._format(code).decode()
        lines = result.rstrip().split("\n")
        for line in lines:
            self.assertLessEqual(len(line), 80, repr(line))
        attr_lines = [l for l in lines if "&" in l]
        self.assertGreaterEqual(len(attr_lines), 2, "Expected attrs to wrap")
        col0 = attr_lines[0].replace('\t', '        ').index("&")
        col1 = attr_lines[1].replace('\t', '        ').index("&")
        self.assertEqual(col0, col1, "Attrs should align to same column")

    def test_bracket_expr_flows_elements_across_lines(self):
        """Long [...] expression flows elements with line-breaking, not one-per-line."""
        code = (
            b'event some_handler(rec: SomeModule::Info)\n'
            b'\t{\n'
            b'\tif ( [aaa, bbb, ccc, rec$source_field, rec$type_field, rec$name_field,\n'
            b'\t      rec$orig_flag, rec$byte_count] in some_long_cache_name )\n'
            b'\t\treturn;\n'
            b'\t}\n'
        )
        result = self._format(code).decode()
        lines = result.rstrip().split('\n')
        # All lines under 80 columns
        for line in lines:
            self.assertLessEqual(len(line), 80, f"Line too long: {repr(line)}")
        self.assertNotIn("MISINDENTATION", result)
        # Elements should flow (multiple per line), not one-per-line
        if_line = [l for l in lines if l.strip().startswith("if")][0]
        self.assertIn("[aaa, bbb, ccc,", if_line)
        # Continuation aligns to column after '['
        cont_line = [l for l in lines if "rec$orig_flag" in l][0]
        self.assertIn("rec$orig_flag, rec$byte_count]", cont_line)

    def test_index_expr_continuation_aligns_after_bracket(self):
        """Index expr continuation lines align to column after '['."""
        code = (
            b'event some_handler(rec: SomeModule::Info)\n'
            b'\t{\n'
            b'\tadd some_long_cache[aaa, bbb, ccc, rec$source_field,'
            b' rec$type_field, rec$name_field, rec$orig_flag, rec$byte_count];\n'
            b'\t}\n'
        )
        result = self._format(code).decode()
        lines = result.rstrip().split('\n')
        for line in lines:
            self.assertLessEqual(len(line), 80, f"Line too long: {repr(line)}")
        self.assertNotIn("MISINDENTATION", result)
        # Continuation should align to column after '['
        add_line = [l for l in lines if "add " in l][0]
        bracket_col = add_line.index('[') + 1
        cont_line = [l for l in lines if l.strip().startswith("rec$")][0]
        content_col = len(cont_line) - len(cont_line.lstrip())
        self.assertEqual(content_col, bracket_col,
                         f"Expected align at col {bracket_col}, got {content_col}")

    def test_record_constructor_aligns_with_preceding_arg(self):
        """Record constructor in func call aligns with preceding argument."""
        code = (
            b'event zeek_init()\n'
            b'\t{\n'
            b'\tLog::create_stream(SOME_LOG,\n'
            b'\t\t[$columns=SomeInfo, $path="some_log_path",'
            b' $policy=some_log_policy]);\n'
            b'\t}\n'
        )
        result = self._format(code).decode()
        lines = result.rstrip().split('\n')
        for line in lines:
            self.assertLessEqual(len(line), 80, f"Line too long: {repr(line)}")
        self.assertNotIn("MISINDENTATION", result)
        # The '[' should stay on the same line as the preceding arg
        call_line = [l for l in lines if "create_stream" in l][0]
        self.assertIn("[$columns", call_line)

    def test_record_constructor_stays_inline_when_fits(self):
        """Record constructor [ stays on same line when first field fits."""
        code = (
            b'event zeek_init()\n'
            b'\t{\n'
            b'\tLog::create_stream(LOG, [$columns=Conn::Info,\n'
            b'\t                $path=path, $policy=Conn::log_policy]);\n'
            b'\t}\n'
        )
        result = self._format(code).decode()
        lines = result.rstrip().split('\n')
        for line in lines:
            self.assertLessEqual(len(line), 80, f"Line too long: {repr(line)}")
        self.assertNotIn("MISINDENTATION", result)
        # [ stays on the same line as LOG
        call_line = [l for l in lines if "create_stream" in l][0]
        self.assertIn("LOG, [$columns", call_line)
        # Fields wrap aligned after [
        cont_line = lines[lines.index(call_line) + 1]
        self.assertIn("$policy=", cont_line)

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

    def test_comma_stays_with_value_not_on_next_line(self):
        """Comma should stay on same line as its value, not start the next line."""
        code = b'event foo() { SomeModule::seen([$some_field=SomeModule::build_value(c$some_data$items[stream]), $some_type=SomeModule::URL, $conn=c, $where=SomeModule::IN_URL]); }'
        result = self._format(code).decode()
        lines = result.splitlines()
        # Find the line with build_value — the comma should be at the end
        val_line = next(l for l in lines if "build_value" in l)
        self.assertTrue(val_line.rstrip().endswith(","),
                        f"Comma not at end of line: {val_line!r}")
        # No line should start with a comma
        for line in lines:
            self.assertFalse(line.lstrip().startswith(","),
                             f"Line starts with comma: {line!r}")

    def test_no_format_before_statement(self):
        """#@ NO-FORMAT before a statement preserves its original formatting."""
        code = b'#@ NO-FORMAT\nglobal x =    1;\nglobal y = 2;\n'
        result = self._format(code).decode()
        self.assertIn("global x =    1;", result)
        self.assertIn("#@ NO-FORMAT", result)
        # Second statement should be formatted normally
        self.assertIn("global y = 2;", result)

    def test_no_format_trailing_comment(self):
        """Trailing #@ NO-FORMAT preserves the statement's original formatting."""
        code = b'global x =    1; #@ NO-FORMAT\nglobal y = 2;\n'
        result = self._format(code).decode()
        self.assertIn("global x =    1; #@ NO-FORMAT", result)
        self.assertIn("global y = 2;", result)

    def test_begin_end_no_format_range(self):
        """BEGIN/END-NO-FORMAT preserves formatting for the entire range."""
        code = b'global a = 1;\n#@ BEGIN-NO-FORMAT\nglobal x =    1;\nglobal y =    2;\n#@ END-NO-FORMAT\nglobal z = 3;\n'
        result = self._format(code).decode()
        self.assertIn("global x =    1;", result)
        self.assertIn("global y =    2;", result)
        self.assertIn("#@ BEGIN-NO-FORMAT", result)
        self.assertIn("#@ END-NO-FORMAT", result)
        # Surrounding statements formatted normally
        self.assertIn("global a = 1;", result)
        self.assertIn("global z = 3;", result)

    def test_begin_end_no_format_in_export(self):
        """BEGIN/END-NO-FORMAT inside export block should not duplicate END annotation."""
        code = (
            b'export {\n'
            b'\t#@ BEGIN-NO-FORMAT\n'
            b'\ttype SomeType: enum {\n'
            b'\t\tVAL_A,\n'
            b'\t\tVAL_B\n'
            b'\t};\n'
            b'\t#@ END-NO-FORMAT\n'
            b'}\n'
        )
        result = self._format(code).decode()
        self.assertEqual(result.count("#@ END-NO-FORMAT"), 1,
                         "END-NO-FORMAT should appear exactly once")
        self.assertIn("#@ BEGIN-NO-FORMAT", result)
        self.assertIn("type SomeType", result)

    def test_no_format_unbalanced_begin(self):
        """Unbalanced BEGIN-NO-FORMAT without END should raise an error."""
        code = b'#@ BEGIN-NO-FORMAT\nglobal x = 1;\n'
        with self.assertRaises(ValueError) as ctx:
            self._format(code)
        self.assertIn("without matching #@ END-NO-FORMAT", str(ctx.exception))

    def test_no_format_unbalanced_end(self):
        """END-NO-FORMAT without BEGIN should raise an error."""
        code = b'#@ END-NO-FORMAT\nglobal x = 1;\n'
        with self.assertRaises(ValueError) as ctx:
            self._format(code)
        self.assertIn("without matching #@ BEGIN-NO-FORMAT", str(ctx.exception))

    def test_no_format_inside_begin_end(self):
        """NO-FORMAT inside BEGIN/END range should raise an error."""
        code = b'#@ BEGIN-NO-FORMAT\n#@ NO-FORMAT\nglobal x = 1;\n#@ END-NO-FORMAT\n'
        with self.assertRaises(ValueError) as ctx:
            self._format(code)
        self.assertIn("inside #@ BEGIN-NO-FORMAT region", str(ctx.exception))

    def test_no_format_trailing_in_func_body(self):
        """Trailing #@ NO-FORMAT inside a function body uses correct indentation."""
        code = (
            b'hook SomeModule::some_log_policy()\n'
            b'    {\n'
            b'    some_code();\n'
            b'\n'
            b'    local o_s = rec_id$orig_h/subnet_mask; #@ NO-FORMAT\n'
            b'    local r_s = rec_id$resp_h/subnet_mask; #@ NO-FORMAT\n'
            b'\n'
            b'    more_code();\n'
            b'    }\n'
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        # Both NO-FORMAT lines should be tab-indented and preserve original content
        no_fmt_lines = [l for l in lines if "#@ NO-FORMAT" in l]
        self.assertEqual(len(no_fmt_lines), 2)
        for line in no_fmt_lines:
            self.assertTrue(line.startswith("\t"), f"Expected tab indent: {line!r}")
            self.assertIn("/subnet_mask; #@ NO-FORMAT", line)

    def test_no_format_sole_statement_in_func_body(self):
        """NO-FORMAT on the only statement in a function body gets tab indented."""
        code = (
            b'function some_fn(h: addr): string\n'
            b'    {\n'
            b'    return some_call(h) ? other_call(h/some_mask) : "fallback"; #@ NO-FORMAT\n'
            b'    }\n'
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        no_fmt_lines = [l for l in lines if "#@ NO-FORMAT" in l]
        self.assertEqual(len(no_fmt_lines), 1)
        self.assertTrue(no_fmt_lines[0].startswith("\t"),
                        f"Expected tab indent: {no_fmt_lines[0]!r}")

    def test_record_constructor_breaks_before_bracket_when_deep(self):
        """Record constructor [$field=val] moves to next line when fields would overflow."""
        code = (
            b'event some_evt()\n'
            b'    {\n'
            b'    SomeModule::setup_stream(SOME_STREAM_LOG, [$columns=SomeStreamInfo,\n'
            b'                                               $path="some_long_module_stream_path",\n'
            b'                                               $policy=some_stream_log_policy\n'
            b'                       ]);\n'
            b'    }\n'
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        # The [ should be on the continuation line, not on the first line
        call_line = [l for l in lines if "setup_stream" in l][0]
        self.assertNotIn("[", call_line)
        # All lines should fit under 80
        for line in lines:
            self.assertLessEqual(len(line), 80, f"Line too long: {line!r}")

    def test_record_field_after_multiline_value_starts_new_line(self):
        """After a record field whose value spans multiple lines, the next field starts on a fresh line."""
        code = (
            b"event some_evt()\n"
            b"\t{\n"
            b"\tSOME_CALL([$field_a=Some_Enum_Val,\n"
            b"\t           $field_b=fmt(\"some format string with several placeholders"
            b" %d items across %d groups within %s window\",\n"
            b"\t                        val_one, val_two, val_three),\n"
            b"\t           $field_c=endpoints, $field_d=src_host, $field_e=cat(src_host)]);\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        # $field_c should NOT be on the same line as val_three's closing paren
        val_three_line = [l for l in lines if "val_three" in l][0]
        self.assertNotIn("$field_c", val_three_line)
        # $field_c should be on its own line, aligned with $field_a
        field_c_line = [l for l in lines if "$field_c" in l][0]
        field_a_line = [l for l in lines if "$field_a" in l][0]
        self.assertEqual(field_c_line.index("$field_c"),
                         field_a_line.index("$field_a"))

    def test_record_field_after_moderately_wide_multiline_value(self):
        """Record field that's under 80 chars flat but overflows at alignment still triggers line break."""
        code = (
            b"function some_func()\n"
            b"\t{\n"
            b"\tSOME_CALL([$field_a=Some_Enum_Val,\n"
            b"\t           $field_b=fmt(\"total items: %d, associated ids: %s\",\n"
            b"\t                        total_items, conn_ids),\n"
            b"\t           $field_c=src_host, $field_d=cat(src_host, bucket)]);\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        # $field_c should NOT be on the same line as conn_ids's closing paren
        conn_ids_line = [l for l in lines if "conn_ids" in l][0]
        self.assertNotIn("$field_c", conn_ids_line)

    def test_deep_func_call_args_align_to_paren(self):
        """Function call args at deep nesting align to '(' not bare tab."""
        code = (
            b"function foo()\n"
            b"\t{\n"
            b"\tif ( some_condition() )\n"
            b"\t\t{\n"
            b"\t\tlocal published = Some::long_func_name(proxy_pool,\n"
            b"\t\t\tcat(c$id$orig_h, fqdn), SomeModule::some_handler,\n"
            b"\t\t\tc$id$orig_h, c$uid, fqdn, conn$orig_bytes);\n"
            b"\t\t}\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        # Args should be aligned to after '(', not at bare tab indentation
        arg_lines = [l for l in lines
                     if l.lstrip().startswith(("cat(", "c$", "SomeModule", "conn$", "proxy_pool"))
                     and "long_func_name" not in l]
        if arg_lines:
            indents = set(len(l) - len(l.lstrip()) for l in arg_lines)
            self.assertEqual(len(indents), 1,
                             f"Args should be consistently aligned, got lines: {arg_lines}")

    def test_export_trailing_comments_indented(self):
        """Comments at end of export block should be indented like declarations."""
        code = b'export {\n\tglobal some_evt: event(rec: Info);\n\n\t## Some trailing comment\n\t## Another trailing comment\n}\n'
        result = self._format(code).decode()
        lines = result.splitlines()
        for line in lines:
            if line.lstrip().startswith("##"):
                self.assertTrue(line.startswith("\t"),
                                f"Comment not indented: {line!r}")


    def test_record_args_fill_line_before_wrapping(self):
        """Record-style $field=value args should fill the line before wrapping."""
        code = (
            b'function some_fn()\n'
            b'    {\n'
            b'    local info = SomeModule::SomeFn($note=Found, $uid=uid,\n'
            b'            $msg=fmt("%s found on %s entity using %s item.",\n'
            b'                usecase_desc, orig_entity, item),\n'
            b'            $sub=fmt("Score: %s. Days: %s",\n'
            b'                item_score, history_days),\n'
            b'            $identifier=cat(orig_entity, usecase, item));\n'
            b'    }\n'
        )
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        lines = result.splitlines()
        # $note=Found and $uid=uid should be on the same line
        first_field_line = [l for l in lines if '$note=' in l]
        self.assertEqual(len(first_field_line), 1)
        self.assertIn('$uid=uid', first_field_line[0],
                       "$uid=uid should be on the same line as $note=Found")


    def test_record_args_break_after_equals_when_deep(self):
        """Deep record-style args break after '=' and align to '('."""
        code = (
            b'function some_fn()\n'
            b'    {\n'
            b'    if ( some_condition )\n'
            b'        {\n'
            b'        local info = SomeLongModule::SomeFn($ts=ts, $uid=uid,\n'
            b'            $use_case=usecase,\n'
            b'            $use_case_description=usecase_desc,\n'
            b'            $entity_training_items=entity_training_items,\n'
            b'            $entity=entity, $original_entity=original_entity,\n'
            b'            $item=item,\n'
            b'            $first_seen_type=some_enum_map[some_type],\n'
            b'            $history_days=history_days, $history=history);\n'
            b'        }\n'
            b'    }\n'
        )
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        lines = result.splitlines()
        # Should break after '='
        eq_line = [l for l in lines if 'info =' in l]
        self.assertEqual(len(eq_line), 1)
        self.assertTrue(eq_line[0].rstrip().endswith('='),
                        "Should break after '='")
        # Most args should be aligned to after '(' — some may be shrunk
        # left to fit within 80 columns when the arg is wide
        arg_lines = [l for l in lines if l.lstrip().startswith("$") and "info =" not in l]
        indents = [len(l) - len(l.lstrip()) for l in arg_lines]
        # The majority of args share the same (max) indent
        max_indent = max(indents)
        normal_count = sum(1 for i in indents if i == max_indent)
        self.assertGreater(normal_count, len(indents) // 2,
                           f"Majority of args should share alignment, got {indents}")
        # Very short fields may share a line
        ts_line = [l for l in lines if '$ts=' in l]
        self.assertEqual(len(ts_line), 1)
        self.assertIn('$uid=', ts_line[0],
                       "$ts and $uid should share a line")


    def test_assignment_continuation_aligns_after_equals(self):
        """Assignment RHS continuation aligns to column after '= '."""
        code = (
            b'event some_evt()\n'
            b'    {\n'
            b'    si$successful_entities = si$successful_entities |\n'
            b'                             worker_si$successful_entities;\n'
            b'    }\n'
        )
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        lines = result.splitlines()
        # RHS wraps at '= ' alignment column (inline, not break at '=')
        assign_line = [l for l in lines if '= si$' in l][0]
        cont_line = lines[lines.index(assign_line) + 1]
        eq_pos = assign_line.expandtabs(8).index('= ') + 2
        cont_col = len(cont_line.expandtabs(8)) - len(cont_line.expandtabs(8).lstrip())
        self.assertEqual(cont_col, eq_pos)


    def test_assignment_breaks_at_equals_when_rhs_call_is_deep(self):
        """Assignment breaks at '=' when RHS function call would cause MISINDENTATION."""
        code = (
            b"function foo()\n"
            b"\t{\n"
            b"\tfor ( x in xs )\n"
            b"\t\tfor ( y in ys )\n"
            b"\t\t\tsome_long_table[some_key] =\n"
            b"\t\t\t\t\tSomeFunc($field_a=val_a, $field_b=val_b);\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        lines = result.splitlines()
        # Should break after '=' with RHS on next line
        eq_line = [l for l in lines if 'some_key] =' in l][0]
        self.assertTrue(eq_line.rstrip().endswith('='))
        # RHS should be on the next line, flat
        rhs_line = lines[lines.index(eq_line) + 1]
        self.assertIn('SomeFunc(', rhs_line)
        self.assertIn('$field_b=val_b)', rhs_line)

    def test_initializer_continuation_aligns_after_equals(self):
        """Initializer RHS continuation aligns to column after '= '."""
        code = (
            b'event some_evt()\n'
            b'    {\n'
            b'    local some_services = worker_si$some_entities |\n'
            b'                          worker_si$other_entities;\n'
            b'    }\n'
        )
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        lines = result.splitlines()
        assign_line = [l for l in lines if '= worker_si$' in l][0]
        cont_line = lines[lines.index(assign_line) + 1]
        eq_pos = assign_line.expandtabs(8).index('= ') + 2
        cont_col = len(cont_line.expandtabs(8)) - len(cont_line.expandtabs(8).lstrip())
        self.assertEqual(cont_col, eq_pos)


    def test_lambda_with_capture_list(self):
        """Lambda with capture list [var] should preserve the capture list."""
        code = (
            b'function foo()\n'
            b'    {\n'
            b'    some_handler(opt_id,\n'
            b'        function[evt_grp](id: string, val: bool): bool\n'
            b'            {\n'
            b'            return val;\n'
            b'            });\n'
            b'    }\n'
        )
        result = self._format(code).decode()
        self.assertIn('[evt_grp]', result,
            "Capture list should be preserved in lambda")
        self.assertIn('function[evt_grp](', result,
            "Capture list should appear between 'function' and '('")

    def test_lambda_arg_starts_on_own_line(self):
        """Lambda args (not first) start on their own continuation line."""
        code = (
            b'event zeek_init()\n'
            b'    {\n'
            b'    register_handler(some_usecase, function(h: addr, si: SomeInfo)\n'
            b'        {\n'
            b'        print h;\n'
            b'        }, 6.0);\n'
            b'    }\n'
        )
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        lines = result.splitlines()
        # The lambda head should be on its own line (after the comma)
        lambda_line = [l for l in lines if 'function(' in l][0]
        comma_line = [l for l in lines if 'some_usecase,' in l][0]
        self.assertNotIn('function(', comma_line)
        self.assertIn('function(', lambda_line)

    def test_lambda_first_arg_stays_inline(self):
        """Lambda as first argument stays on the same line as the call."""
        code = (
            b'event zeek_init()\n'
            b'    {\n'
            b'    register_handler(function(h: addr, si: SomeInfo)\n'
            b'        {\n'
            b'        print h;\n'
            b'        }, 6.0);\n'
            b'    }\n'
        )
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        lines = result.splitlines()
        call_line = [l for l in lines if 'register_handler(' in l][0]
        self.assertIn('function(', call_line)


    def test_boolean_op_not_blocked_by_bracket(self):
        """&& before [...] stays at end of line, not pushed to start of next."""
        code = (
            b'event some_evt(si: SomeInfo)\n'
            b'\t{\n'
            b'\tif ( coal_max_entries > 0 && |coalesced_state| >= coal_max_entries &&\n'
            b'\t     [server_name, server_subj, server_issuer, client_subj,\n'
            b'\t      client_issuer, ja3] !in coalesced_state )\n'
            b'\t\treturn;\n'
            b'\t}\n'
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        # The && should be at end of line, not start of next
        and_line = [l for l in lines if '&& [' in l or 'coal_max_entries &&' in l]
        self.assertTrue(any(l.rstrip().endswith('&&') for l in and_line),
                        f"Expected && at end of line, got: {and_line}")
        # Continuation inside [...] aligns one past [
        bracket_line = [l for l in lines if '[server_name' in l][0]
        bracket_col = bracket_line.expandtabs(8).index('[')
        cont_line = [l for l in lines if 'client_issuer' in l][0]
        cont_col = len(cont_line.expandtabs(8)) - len(cont_line.expandtabs(8).lstrip())
        self.assertEqual(cont_col, bracket_col + 1)

    def test_record_constructor_comments_aligned_with_fields(self):
        """Comments between record constructor fields align with $ fields."""
        code = (
            b'event some_evt(c: connection)\n'
            b'\t{\n'
            b'\tNOTICE([$note=Some_Notice,\n'
            b'\t\t$conn=c,\n'
            b'\t\t# This is a comment about the next field.\n'
            b'\t\t# Another comment line.\n'
            b'\t\t$identifier=cat(id$orig_h, id$resp_h)]);\n'
            b'\t}\n'
        )
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        lines = result.splitlines()
        # [ should stay on first line with NOTICE(
        notice_line = [l for l in lines if 'NOTICE(' in l][0]
        self.assertIn('[$note=', notice_line)
        # Comments should be at same alignment as $ fields
        field_line = [l for l in lines if '$conn=' in l][0]
        field_col = field_line.expandtabs(8).index('$')
        comment_lines = [l for l in lines if l.strip().startswith('#')]
        for cl in comment_lines:
            comment_col = cl.expandtabs(8).index('#')
            self.assertEqual(comment_col, field_col,
                             f"Comment misaligned: {repr(cl)}")

    def test_typelist_wraps_when_long(self):
        """Long table[...] type list wraps with alignment after [."""
        code = (
            b'global some_state: table[SomeModule::CertInfo,'
            b' SomeModule::AltNameInfo, SomeModule::ConstraintInfo]'
            b' of count &default=0;\n'
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        # The line with table[ should end with a comma (wrapped)
        table_line = [l for l in lines if 'table[' in l][0]
        self.assertTrue(table_line.rstrip().endswith(','))
        # Continuation aligns after [
        bracket_col = table_line.expandtabs(8).index('[') + 1
        cont_line = lines[lines.index(table_line) + 1]
        cont_col = len(cont_line.expandtabs(8)) - len(cont_line.expandtabs(8).lstrip())
        self.assertEqual(cont_col, bracket_col)

    def test_long_regex_no_misindentation(self):
        """Long unbreakable regex pattern doesn't produce MISINDENTATION."""
        code = (
            b'const some_long_pattern_name = '
            b'/\\x4c\\x00\\x6f\\x00\\x67\\x00\\x69\\x00\\x6e\\x00'
            b'\\x20\\x00\\x66\\x00\\x61\\x00\\x69\\x00\\x6c\\x00'
            b'\\x65\\x00\\x64\\x00\\x20\\x00\\x66\\x00\\x6f\\x00'
            b'\\x72\\x00\\x20\\x00\\x75\\x00\\x73\\x00\\x65\\x00'
            b'\\x72\\x00/;\n'
        )
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        self.assertIn("some_long_pattern_name", result)


    def test_call_args_balanced_break(self):
        """Call args break at a balanced point, not greedily at the last fit."""
        code = (
            b"function some_func(val: string)\n"
            b"\t{\n"
            b"\tif ( SomeModule::check_ready() )\n"
            b"\t\tSomeModule::send_msg(pt, some_long_handler,\n"
            b"\t\t                     to_addr(rec$some_field), p);\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        # Break should be after some_long_handler, not before p
        call_line = [l for l in lines if "send_msg" in l][0]
        self.assertTrue(call_line.rstrip().endswith("some_long_handler,"),
                        f"Expected break after some_long_handler, got: {repr(call_line)}")

    def test_blank_line_before_else_preserved(self):
        """Blank line before else clause is preserved when present in source."""
        code = (
            b"function some_func(val: string)\n"
            b"\t{\n"
            b"\tif ( some_pattern == val )\n"
            b'\t\tresult = "found";\n'
            b"\n"
            b"\telse\n"
            b'\t\tresult = "default";\n'
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.split("\n")
        else_idx = next(i for i, l in enumerate(lines) if "else" in l)
        self.assertEqual(lines[else_idx - 1].strip(), "",
                         "Expected blank line before else")

    def test_blank_line_before_else_if_with_annotation(self):
        """Blank line before else if preserved when body has trailing annotation."""
        code = (
            b"function some_func(c: connection, name: string,\n"
            b"                   value: string, prefix: string)\n"
            b"\t{\n"
            b'\tif ( name == "some-type" )\n'
            b'\t\tsome_handler(c, value, prefix + "-sfx"); #@ NOT-TESTED\n'
            b"\n"
            b'\telse if ( name == "other-type" )\n'
            b'\t\tother_handler(c, value, prefix + "-sfx");\n'
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.split("\n")
        else_idx = next(i for i, l in enumerate(lines) if "else" in l)
        self.assertEqual(lines[else_idx - 1].strip(), "",
                         "Expected blank line before else if")
        self.assertIn("#@ NOT-TESTED", result)

    def test_no_blank_line_before_else_when_absent(self):
        """No blank line before else when source doesn't have one."""
        code = (
            b"function some_func(val: string)\n"
            b"\t{\n"
            b"\tif ( some_pattern == val )\n"
            b'\t\tresult = "found";\n'
            b"\telse\n"
            b'\t\tresult = "default";\n'
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.split("\n")
        else_idx = next(i for i, l in enumerate(lines) if "else" in l)
        self.assertNotEqual(lines[else_idx - 1].strip(), "",
                            "Should not have blank line before else")

    def test_comment_before_else_preserved(self):
        """Comments before else clause are preserved."""
        code = (
            b"function some_func(val: string)\n"
            b"\t{\n"
            b'\tif ( some_pattern == val )\n'
            b'\t\tresult = "found";\n'
            b"\n"
            b"\t# This handles the fallback case.\n"
            b"\t# Check secondary pattern too.\n"
            b"\telse if ( other_pattern == val )\n"
            b'\t\tresult = val[idx + 1 :];\n'
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertIn("# This handles the fallback case.", result)
        self.assertIn("# Check secondary pattern too.", result)
        # Verify blank line before comments is preserved
        lines = result.split("\n")
        comment_idx = next(i for i, l in enumerate(lines) if "fallback" in l)
        self.assertEqual(lines[comment_idx - 1].strip(), "",
                         "Expected blank line before comment block")

    def test_annotation_on_close_paren_preserved(self):
        """#@ annotation on closing ) of func params is preserved."""
        code = (
            b"event some_handler(c: connection, is_orig: bool,\n"
            b"                   payload: string) #@ NOT-TESTED\n"
            b"\t{\n"
            b'\tadd c$app["test"];\n'
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertIn(") #@ NOT-TESTED", result)

    def test_preproc_after_blank_line_no_extra_blank(self):
        """Preproc directive after a blank line shouldn't gain an extra blank."""
        code = b"module SomeMod;\n\n@load ./const\n"
        result = self._format(code).decode()
        # Should have exactly one blank line between module and @load
        self.assertEqual(result, "module SomeMod;\n\n@load ./const\n")

    def test_vector_constructor_inline(self):
        code = b"const xs: vector of count = {1, 2, 3};"
        result = self._format(code).decode()
        self.assertIn("vector(1, 2, 3)", result)
        self.assertNotIn("{", result)

    def test_vector_constructor_multiline(self):
        # Comments force one-per-line mode
        code = (
            b"const xs: vector of double = {\n"
            b"\t0.01,\n"
            b"\t0.02, # a note\n"
            b"\t0.03,\n"
            b"\t0.04\n"
            b"};"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        # First line: declaration with vector(
        self.assertIn("= vector(", lines[0])
        # Items indented one tab
        self.assertTrue(lines[1].startswith("\t"))
        self.assertIn("0.01,", lines[1])
        # Closing ) at column 0
        self.assertEqual(lines[-1], ");")

    def test_vector_constructor_with_comments(self):
        code = (
            b"const xs: vector of double = {\n"
            b"\t0.01,\n"
            b"\t0.02, # note\n"
            b"\t0.03\n"
            b"};"
        )
        result = self._format(code).decode()
        self.assertIn("vector(", result)
        self.assertIn("# note", result)

    def test_table_constructor_inline(self):
        code = b'const tbl: table[count] of string = {[1] = "a"};'
        result = self._format(code).decode()
        self.assertIn('table([1] = "a")', result)
        self.assertNotIn("{", result)

    def test_table_constructor_multiline(self):
        code = (
            b'const tbl: table[count] of string = {\n'
            b'\t[0x00] = "AAAA",\n'
            b'\t[0x01] = "BBBB",\n'
            b'\t[0x02] = "CCCC",\n'
            b'\t[0x03] = "DDDD",\n'
            b'\t[0x04] = "EEEE",\n'
            b'\t[0x05] = "FFFF"\n'
            b'};'
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        self.assertIn("= table(", lines[0])
        self.assertEqual(lines[-1], ");")

    def test_set_constructor_inline(self):
        code = b"const ss: set[addr] = {1.2.3.4, 5.6.7.8};"
        result = self._format(code).decode()
        self.assertIn("set(1.2.3.4, 5.6.7.8)", result)

    def test_set_constructor_no_type(self):
        # Without explicit type, auto-detect set from content
        code = b"const ss = {1.2.3.4, 5.6.7.8};"
        result = self._format(code).decode()
        self.assertIn("set(", result)

    def test_not_in_breaks_after_operator(self):
        # !in breaks like other binary ops: operator stays on first line
        code = (
            b"function some_func(a: int)\n"
            b"\t{\n"
            b"\tif ( some_very_long_access_to_a_member[foo]"
            b" !in SomeModule::other$nested_field )\n"
            b"\t\t{\n"
            b'\t\tprint "hi";\n'
            b"\t\t}\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        not_in_lines = [l for l in lines if "!in" in l]
        self.assertEqual(len(not_in_lines), 1)
        # Operator stays at end of first line, operand on continuation
        self.assertTrue(not_in_lines[0].rstrip().endswith("!in"))

    def test_not_in_stays_inline_when_short(self):
        code = (
            b"function some_func()\n"
            b"\t{\n"
            b"\tif ( x !in y )\n"
            b'\t\tprint "hi";\n'
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertIn("x !in y", result)

    def test_long_slice_breaks_at_colon(self):
        # Long index slices break after ':' with RHS aligned under LHS
        code = (
            b"function some_func()\n"
            b"\t{\n"
            b"\tlocal val = some_long_func_name("
            b"data[some$off - 1 + 8 : some$off - 1 + 10 + 23 / 15 - 3]);\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        slice_lines = [l for l in lines if "some$off" in l]
        # Should break after ':'
        self.assertTrue(slice_lines[0].rstrip().endswith(":"))
        # RHS should align with LHS (after '[')
        self.assertEqual(len(slice_lines), 2)
        lhs_col = slice_lines[0].index("[") + 1
        rhs_col = len(slice_lines[1]) - len(slice_lines[1].lstrip())
        self.assertEqual(lhs_col, rhs_col)

    def test_trailing_comment_on_print(self):
        code = b'print "hello"; # a note\n'
        result = self._format(code).decode()
        self.assertIn('print "hello"; # a note', result)

    def test_trailing_comment_on_return(self):
        code = (
            b"function some_func(): count\n"
            b"\t{\n"
            b"\treturn 42; # the answer\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertIn("return 42; # the answer", result)

    def test_trailing_comment_on_next(self):
        code = (
            b"event some_evt()\n"
            b"\t{\n"
            b"\tfor (val in some_set)\n"
            b"\t\tnext; # skip it\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertIn("next; # skip it", result)

    def test_trailing_comment_on_add_delete(self):
        code = (
            b"event some_evt()\n"
            b"\t{\n"
            b"\tadd some_set[val]; # track it\n"
            b"\tdelete some_set[val]; # remove it\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertIn("add some_set[val]; # track it", result)
        self.assertIn("delete some_set[val]; # remove it", result)

    def test_trailing_comment_on_local(self):
        code = (
            b"event some_evt()\n"
            b"\t{\n"
            b"\tlocal x = 1; # init\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertIn("local x = 1; # init", result)

    def test_trailing_comment_on_module(self):
        code = b"module SomeMod; # main module\n"
        result = self._format(code).decode()
        self.assertIn("module SomeMod; # main module", result)

    def test_trailing_comment_on_type_decl(self):
        code = b"type val: count; # a counter\n"
        result = self._format(code).decode()
        self.assertIn("type val: count; # a counter", result)

    def test_trailing_comment_on_assert(self):
        code = (
            b"event some_evt()\n"
            b"\t{\n"
            b"\tassert 1 == 1; # sanity\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertIn("assert 1 == 1; # sanity", result)

    def test_comment_only_file(self):
        code = (
            b"#\n"
            b"# A comment-only file.\n"
            b"#\n"
        )
        result = self._format(code).decode()
        self.assertIn("# A comment-only file.", result)
        self.assertEqual(result.count("#"), 3)

    def test_trailing_comment_on_open_brace(self):
        code = (
            b"event some_evt()\n"
            b"\t{ # Start tracking.\n"
            b"\tprint 1;\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertIn("{ # Start tracking.", result)

    def test_trailing_comment_on_if_close_paren(self):
        code = (
            b"function some_func(a: count, b: string, rec: SomeRec)\n"
            b"\t{\n"
            b"\tif ( rec in did_check ) #@ BEGIN-SKIP-TESTING\n"
            b"\t\treturn;\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertIn(") #@ BEGIN-SKIP-TESTING", result)

    def test_trailing_comment_on_for_close_paren(self):
        code = (
            b"event some_evt()\n"
            b"\t{\n"
            b"\tfor ( idx in some_list ) # iterate items\n"
            b"\t\tprint idx;\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertIn(") # iterate items", result)

    def test_trailing_comment_on_while_close_paren(self):
        code = (
            b"event some_evt()\n"
            b"\t{\n"
            b"\twhile ( some_flag ) # keep going\n"
            b"\t\tprint 1;\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertIn(") # keep going", result)

    def test_blank_line_before_trailing_comment_preserved(self):
        code = (
            b"#@ BEGIN-SKIP-TESTING\n"
            b"\n"
            b"function some_func(val: string)\n"
            b"\t{\n"
            b"\tsome_call(val);\n"
            b"\t}\n"
            b"\n"
            b"#@ END-SKIP-TESTING\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        # Find the } and #@ END-SKIP-TESTING lines
        for i, line in enumerate(lines):
            if line.strip() == "}":
                # Blank line then comment
                self.assertEqual(lines[i + 1].strip(), "")
                self.assertIn("#@ END-SKIP-TESTING", lines[i + 2])
                break
        else:
            self.fail("} not found in output")

    def test_no_blank_line_before_comment_after_block(self):
        code = (
            b"function some_func(a: count, b: string, rec: SomeRec)\n"
            b"\t{\n"
            b"\t#@ BEGIN-SKIP-TESTING\n"
            b"\tif ( rec in did_check )\n"
            b"\t\treturn;\n"
            b"\t#@ END-SKIP-TESTING\n"
            b"\n"
            b"\tnext_thing();\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        # Find the return; and #@ END-SKIP-TESTING lines
        for i, line in enumerate(lines):
            if "return;" in line:
                # Next line should be #@ END-SKIP-TESTING with no blank line
                self.assertIn("#@ END-SKIP-TESTING", lines[i + 1])
                break
        else:
            self.fail("return; not found in output")
        # The blank line after #@ END-SKIP-TESTING should be preserved
        self.assertIn("", lines)  # at least one blank line exists

    def test_func_params_overflow_shifted_left(self):
        """Wide param shifts left to stay within 80; other params stay aligned."""
        code = (
            b"event SomeModule::Geneve::some_filtered_option("
            b"inner_c: connection,\n"
            b"\tinner_hdr: pkt_hdr, vni: count,\n"
            b"\tflags: count,\n"
            b"\topt: SomeModule::Geneve::some_geneve_hdr_option)\n"
            b"\t{\n\t}\n"
        )
        result = self._format(code).decode()
        lines = result.rstrip().split("\n")
        for line in lines:
            self.assertLessEqual(len(line), 80, repr(line))
        # Most params paren-align at the same column
        normal_lines = [l for l in lines if "inner_hdr:" in l or "flags:" in l]
        cols = [len(l) - len(l.lstrip()) for l in normal_lines]
        self.assertEqual(cols[0], cols[1], "Normal params should share alignment")
        # The wide param is shifted left but still space-aligned
        wide_line = [l for l in lines if "some_geneve_hdr_option" in l][0]
        wide_col = len(wide_line) - len(wide_line.lstrip())
        self.assertLess(wide_col, cols[0], "Wide param should be less indented")
        self.assertGreater(wide_col, 0, "Wide param should still be indented")

    def test_event_attr_wraps_when_header_overflows(self):
        code = (
            b'event ssl_extension(c: connection, is_client: bool,'
            b' code: count, val: string) &group="doh-generic"\n'
            b"\t{\n\t}\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        # Params line ends with )
        self.assertTrue(lines[0].rstrip().endswith(")"))
        self.assertLessEqual(len(lines[0]), 80)
        # Attr on continuation line, aligned with params
        self.assertIn('&group="doh-generic"', lines[1])
        paren_col = lines[0].index("(") + 1
        attr_col = len(lines[1]) - len(lines[1].lstrip())
        self.assertEqual(paren_col, attr_col)

    def test_constructor_call_multiline_dedent(self):
        # set(...) already in constructor form uses dedent multiline layout
        code = (
            b'redef some_hosts += set(\n'
            b'\t"some.host.example.com",\n'
            b'\t"another.longer.hostname.example.org",\n'
            b'\t"yet.another.host.example.net",\n'
            b');\n'
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        # First line: redef ... set(
        self.assertIn("set(", lines[0])
        # Items at one-tab indent
        self.assertTrue(lines[1].startswith("\t"))
        # Closing ) at column 0
        self.assertEqual(lines[-1], ");")

    def test_constructor_call_nested_in_export(self):
        # Inside export {}, items get extra tab from enclosing block
        code = (
            b"export {\n"
            b'\toption some_hosts = set(\n'
            b'\t\t"some.host.example.com",\n'
            b'\t\t"another.longer.hostname.example.org",\n'
            b'\t\t"yet.another.host.example.net"\n'
            b"\t);\n"
            b"}\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        # Items at two-tab indent (export + constructor)
        self.assertTrue(lines[2].startswith("\t\t"))
        # Closing ); at one-tab indent
        self.assertEqual(lines[-2], "\t);")

    def test_constructor_call_inline_when_short(self):
        code = b'redef foo += set("a", "b", "c");\n'
        result = self._format(code).decode()
        self.assertEqual(result.strip(), 'redef foo += set("a", "b", "c");')

    def test_record_constructor_wraps_fields_in_call(self):
        # Record constructor arg stays on same line as preceding args,
        # with its fields wrapping internally at the aligned column.
        code = (
            b"event zeek_init()\n"
            b"\t{\n"
            b'\tSome::create_stream(SOME::LOG, [$columns=Info, $path="some_path",\n'
            b"\t                               $policy=log_policy]);\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        # '[' stays on same line as first arg
        self.assertIn("SOME::LOG, [$columns=Info", lines[2])
        # All lines fit in 80 columns
        for line in lines:
            self.assertLessEqual(len(line), 80)

    def test_table_constructor_no_type(self):
        # Without explicit type, auto-detect table from [key]=val content
        code = b'const tbl = {[1] = "a", [2] = "b"};'
        result = self._format(code).decode()
        self.assertIn("table(", result)

    def test_has_field_suffix_stays_with_operand(self):
        """?$ suffix stays with its operand, not split onto its own line."""
        # tree-sitter gives ?$ lower precedence than ||, so the boolean
        # chain appears inside the ?$ expression.  The ?$ suffix must be
        # injected into the fill's last item so the fill accounts for
        # its width when deciding where to break.
        code = (
            b"function some_func(c: connection): SomeInfo\n"
            b"\t{\n"
            b"\tlocal rec = c$some_rec;\n"
            b"\tif ( ! rec?$some_field || |rec$some_field| == 0 ||\n"
            b"\t     ! rec$some_field[0]?$some_val )\n"
            b"\t\treturn ci;\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        # ?$some_val stays on the same line as its operand
        val_line = [l for l in lines if "?$some_val" in l][0]
        self.assertIn("rec$some_field[0]?$some_val", val_line)
        # The boolean chain should wrap (not overflow on one line)
        for line in lines:
            self.assertLessEqual(len(line.expandtabs(8)), 80)

    def test_boolean_chain_packs_with_fill(self):
        """&&/|| chains pack multiple operands per line instead of one-per-line."""
        code = (
            b"event zeek_init()\n"
            b"\t{\n"
            b"\tif ( alpha_val > threshold && beta_val > threshold"
            b" && gamma_val > threshold && delta_val > threshold"
            b" && epsilon_val > threshold && zeta_val > threshold )\n"
            b"\t\tprint \"yes\";\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        cond_lines = [l for l in lines if "threshold" in l]
        # Should pack ~2 operands per line (3 lines), not 1 per line (6 lines)
        self.assertLessEqual(len(cond_lines), 3,
                             f"Expected <=3 condition lines, got {len(cond_lines)}")
        for line in cond_lines:
            self.assertLessEqual(len(line.expandtabs(8)), 80, repr(line))

    def test_constructor_attr_follows_closing_paren(self):
        """Attributes on constructor initializers follow ')' with a space."""
        code = (
            b'const some_lookup: table[string] of string = {\n'
            b'    ["some-key-aa"] = "val-aa",\n'
            b'    ["some-key-bb"] = "val-bb",\n'
            b'    ["some-key-cc"] = "val-cc",\n'
            b'    ["some-key-dd"] = "val-dd",\n'
            b'} &default = function(n: string): string\n'
            b'    {\n'
            b'    return fmt("fixme-%s", n);\n'
            b'    };\n'
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        # The ')' line should have &default on the same line
        paren_line = [l for l in lines if l.strip().startswith(")")][0]
        self.assertIn(") &default", paren_line,
                       "&default should follow ) with a space")


    def test_record_blank_line_after_trailing_comment(self):
        """Blank line between record fields preserved when prev field has comment."""
        code = (
            b"type Foo: record {\n"
            b"    aa: string &default=\"\";\n"
            b"    bb: count &default=0; # a comment\n"
            b"\n"
            b"    cc: bool;\n"
            b"    dd: bool &default=F;\n"
            b"};\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        bb_idx = next(i for i, l in enumerate(lines) if "bb:" in l)
        # Blank line after bb's trailing comment, before cc
        self.assertEqual(lines[bb_idx + 1].strip(), "")
        cc_idx = next(i for i, l in enumerate(lines) if "cc:" in l)
        self.assertEqual(cc_idx, bb_idx + 2)

    def test_type_spec_attrs_wrap_when_comment_overflows(self):
        """Record field attrs wrap to continuation when trailing comment causes overflow."""
        code = (
            b"export {\n"
            b"\ttype SomeInfo: record {\n"
            b"\t\tsome_field_name: set[string] &log\n"
            b"\t\t                              &optional; # A trailing comment here\n"
            b"\t};\n"
            b"}\n"
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        for line in lines:
            self.assertLessEqual(len(line.expandtabs(8)), 80,
                                 f"Line too long: {line!r}")
        # Attrs should be on a separate line from the type
        type_line = [l for l in lines if "set[string]" in l][0]
        self.assertNotIn("&log", type_line)

    def test_record_zeekygen_comments_no_extra_blank_lines(self):
        """Record with zeekygen comments before fields should not gain extra blank lines."""
        code = (
            b"type SomeInfo: record {\n"
            b"\t## A timestamp field.\n"
            b"\tts: time &log;\n"
            b"\n"
            b"\t## A unique identifier.\n"
            b"\tuid: string &log;\n"
            b"\n"
            b"\t## A network address.\n"
            b"\taddr_field: addr &log;\n"
            b"};\n"
        )
        result = self._format(code).decode()
        # No double blank lines anywhere
        self.assertNotIn("\n\n\n", result)
        # Each field should be preceded by exactly one blank line + comment
        lines = result.strip().split("\n")
        uid_idx = next(i for i, l in enumerate(lines) if "uid:" in l)
        self.assertEqual(lines[uid_idx - 1].strip(), "## A unique identifier.")
        self.assertEqual(lines[uid_idx - 2].strip(), "")

    def test_comment_after_preproc_not_merged(self):
        """Comment on its own line after @endif stays on its own line."""
        code = (
            b"# Some comment.\n"
            b"#@ BEGIN-SKIP-TESTING\n"
            b'@if ( some_func("/some/path") > 0 )\n'
            b"    @load /some/path\n"
            b"@else\n"
            b"    @load packages/some-pkg\n"
            b"@endif\n"
            b"#@ END-SKIP-TESTING\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        endif_idx = next(i for i, l in enumerate(lines) if "@endif" in l)
        self.assertEqual(lines[endif_idx].strip(), "@endif")
        self.assertEqual(lines[endif_idx + 1].strip(), "#@ END-SKIP-TESTING")


    def test_assignment_breaks_at_equals_for_atomic_rhs(self):
        """When RHS is atomic (no internal breaks) and overflows, break at '='."""
        # local decl with single-arg call that overflows at deep nesting
        code = (
            b"event some_event(some_arg: string)\n"
            b"\t{\n"
            b"\tif ( T )\n"
            b"\t\t{\n"
            b"\t\tif ( T )\n"
            b"\t\t\t{\n"
            b"\t\t\tlocal some_long_name = SomeModule::some_function(some_really_long_argument);\n"
            b"\t\t\t}\n"
            b"\t\t}\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        assign_lines = [l for l in lines if "some_long_name" in l or "some_function" in l]
        self.assertEqual(len(assign_lines), 2, "should break into two lines")
        self.assertIn("=", assign_lines[0])
        self.assertIn("some_function", assign_lines[1])
        # Continuation uses tab indent, not alignment spaces
        self.assertTrue(assign_lines[1].startswith("\t\t\t\t"))

    def test_assignment_atomic_rhs_stays_flat_when_fits(self):
        """Atomic RHS should stay on same line when it fits."""
        code = b"local x = some_func(arg);\n"
        result = self._format(code).decode()
        self.assertIn("local x = some_func(arg);", result)

    def test_assignment_breakable_rhs_unchanged(self):
        """RHS with internal breaks should still use align(), not the atomic path."""
        code = (
            b"event some_event(some_arg: string)\n"
            b"\t{\n"
            b"\tlocal some_name = some_func(arg_one, arg_two, arg_three);\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        # Multi-arg call has break points — should stay on one line when fits
        self.assertIn("some_func(arg_one, arg_two, arg_three)", result)


    def test_fill_balance_three_lines(self):
        """Fill should produce 3 balanced lines, not pack line 2 much longer than line 3."""
        code = (
            b"event some_event()\n"
            b"\t{\n"
            b"\tif ( some_condition )\n"
            b"\t\tSomeModule::some_long_call(some_first_arg,\n"
            b"\t\t                           some_func(aa$xx, aa$yy),\n"
            b"\t\t                           some_other_arg, bb, cc);\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        # The call args should break into 3 lines, not pack
        # some_func(...) and some_other_arg onto the same line
        call_lines = [l for l in lines if "some_func" in l or "some_other_arg" in l]
        self.assertEqual(len(call_lines), 2,
                         f"Expected some_func and some_other_arg on separate lines:\n{result}")

    def test_fill_balance_no_break_when_worse(self):
        """Balance heuristic should not break when breaking makes lines less even."""
        code = (
            b"function some_func()\n"
            b"\t{\n"
            b"\tif ( ! aa?$bb_chain || |aa$bb_chain| == 0 ||\n"
            b"\t     ! aa$bb_chain[0]?$cc )\n"
            b"\t\treturn dd;\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        # The || chain should break after the second ||, not the first.
        # Breaking at the first would leave line 1 short and line 2 long.
        lines = result.strip().split("\n")
        or_lines = [l for l in lines if "||" in l]
        self.assertEqual(len(or_lines), 1,
                         f"Expected both || on the same line:\n{result}")

    def test_fill_balance_no_split_on_short_line(self):
        """Balance should not split a short line that packs fine under max_width."""
        code = (
            b"event some_handler(aa: connection, bb: bool, cc: count, dd: string,\n"
            b"                   ee: string, ff: string, gg: string,\n"
            b"                   hh: string, ii: string, jj: bool)\n"
            b"\t{\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        # All formal args should fit on 3 lines; balance should not
        # split the 3rd line into two when it's well under 80 cols.
        arg_lines = [l for l in lines if ": " in l]
        self.assertEqual(len(arg_lines), 3,
                         f"Expected 3 lines of formal args:\n{result}")

    def test_record_constructor_newline_after_multiline_field(self):
        """After a multiline record field, next field should start on a new line."""
        code = (
            b'event some_evt()\n'
            b'    {\n'
            b'    some_func([$aa=BB,\n'
            b'              $cc=fmt("Host uses a weak certificate with %d bits",\n'
            b'                      key_len),\n'
            b'              $dd=e, $ff=SSL::some_suppression_interval,\n'
            b'              $gg=cat(resp_h, c$id$resp_p, hash, key_len)]);\n'
            b'    }\n'
        )
        result = self._format(code).decode()
        lines = result.splitlines()
        # $dd=e should be on its own line (not after key_len's closing paren)
        paren_line = [l for l in lines if 'key_len),' in l][0]
        self.assertNotIn('$dd=', paren_line,
            f"Next field should not be on same line as multiline field's close paren: {paren_line!r}")

    def test_record_constructor_preserves_annotation_on_comma(self):
        """#@ annotation comments on record field commas must be preserved."""
        code = (
            b"event some_event()\n"
            b"\t{\n"
            b"\tsome_func([$aa=BB, $cc=dd, #@ SOME-TAG\n"
            b"\t           $ee=ff]);\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertIn("#@ SOME-TAG", result)

    def test_assignment_breaks_at_equals_when_rhs_barely_fits_nested(self):
        """Break at '=' when RHS flat width + TAB_SIZE == MAX_WIDTH exactly."""
        code = (
            b"function some_func(): SomeModule::SomeType\n"
            b"\t{\n"
            b"\tif ( some_condition && some_other_condition )\n"
            b"\t\tsome_long_variable_name[some_id] =\n"
            b"\t\t\tSomeModule::SomeType($aa=result$aa,\n"
            b"\t\t\t                     $bb_string=result$bb_string);\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertNotIn("MISINDENTATION", result)
        # Should break at '=' with RHS on next line at nested indent
        self.assertIn("=\n", result)

    def test_fill_balance_no_break_at_low_column(self):
        """Balance should not trigger when the current column is low (< 2/3 max_width)."""
        code = (
            b"function some_func()\n"
            b"\t{\n"
            b"\tif ( some_long_var_aa && some_var$some_field == \"ok\" &&\n"
            b"\t     other?$some_chain && |other$some_chain| > 2 )\n"
            b"\t\t{ }\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        lines = result.strip().split("\n")
        # The && chain should stay on 2 lines, not split into 3
        and_lines = [l for l in lines if "&&" in l]
        self.assertEqual(len(and_lines), 2,
                         f"Expected 2 lines with &&:\n{result}")

    def test_redef_constructor_breaks_at_equals(self):
        """Constructor that would overflow should break at += instead of one long line."""
        code = (
            b"redef SomeModule::some_generic_packet_thresholds += set(skip_conn_packet_threshold);\n"
        )
        result = self._format(code).decode()
        self.assertIn("\n", result.strip(),
                      f"Expected line break but got single line:\n{result}")
        lines = result.strip().split("\n")
        self.assertTrue(lines[0].rstrip().endswith("+="),
                        f"First line should end with +=:\n{result}")

    def test_nested_call_breaks_at_equals(self):
        """Nested call (call-in-call) should break at = to avoid deep internal alignment."""
        code = (
            b"function foo()\n"
            b"\t{\n"
            b"\tlocal next_orig_multiplier = double_to_count(floor(c$orig$size / size_threshold_in_bytes));\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertIn("multiplier =\n", result,
                      f"Expected break after =:\n{result}")
        for line in result.split("\n"):
            self.assertLessEqual(len(line.expandtabs(8)), 80,
                                 f"Line too wide:\n{result}")

    def test_simple_call_no_break_at_equals(self):
        """Simple call (no nested call/index) should NOT break at =."""
        code = (
            b"function foo()\n"
            b"\t{\n"
            b"\tif ( Cluster::is_enabled() )\n"
            b"\t\t{\n"
            b'\t\tlocal pt = Cluster::rr_topic(Cluster::proxy_pool, "application-identification");\n'
            b"\t\t}\n"
            b"\t}\n"
        )
        result = self._format(code).decode()
        self.assertNotIn("pt =\n", result,
                         f"Should not break at =:\n{result}")
        for line in result.split("\n"):
            self.assertLessEqual(len(line.expandtabs(8)), 80,
                                 f"Line too wide:\n{result}")

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
	foo) ();
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


