# Basic call wrapping: simple calls, nested calls, fill layout balancing, consistent wrapping, comment after open-paren.

function some_func(val: string)
	{
	if ( SomeModule::check_ready() )
		SomeModule::send_msg(pt, some_long_handler,
		                     to_addr(rec$some_field), p);
	}

function foo()
	{
	call( # with a comment
	    arg1, arg2);
	}

function foo()
	{
	if ( some_condition() )
		{
		local published = Some::long_func_name(proxy_pool,
			cat(c$id$orig_h, fqdn), SomeModule::some_handler,
			c$id$orig_h, c$uid, fqdn, conn$orig_bytes);
		}
	}

function some_func_a() { if ( foo ) event SomeModule::some_raised_evt(o, label, conf, source, caller); }

event some_event()
	{
	if ( some_condition )
		SomeModule::some_long_call(some_first_arg,
		                           some_func(aa$xx, aa$yy),
		                           some_other_arg, bb, cc);
	}

function f() { local info = SomeModule::SomeRecord($field1=some_very_long_value, $field2=another_long_value); }

function some_func_bb(c: connection, cnt: count): interval { local some_long_variable_a = double_to_count(floor(c$orig$size / some_long_variable_bb)); }

function foo()
	{
	local next_orig_multiplier = double_to_count(floor(c$orig$size / size_threshold_in_bytes));
	}

function some_func_bb(c: connection, cnt: count): interval { if ( some_long_variable_a > 0 ) { SomeModule::some_long_function_name(c, (some_long_variable_a + 3) * some_long_variable_bb, F); } }

function foo()
	{
	if ( Cluster::is_enabled() )
		{
		local pt = Cluster::rr_topic(Cluster::proxy_pool, "application-identification");
		}
	}

function f() { if ( ! some_function_name(some_argument, new_file, extra_vals, some_uids, some_ids, info$source ? info$source : "", info$mime ? info$mime : "", info$md5 ? info$md5 : "") ) print "failed"; }

# Balanced fill: last arg should not be orphaned on its own line.
function check_value()
	{
	if ( foo )
		{
		local pt = Cluster::rr_topic(Cluster::proxy_pool, "application-identification");
		Cluster::publish(pt, mark_server, to_addr(uri$netlocation), p);
		}
	}

# Balanced fill across 3+ lines.
function foo()
	{
	if ( publish_message )
		Cluster::publish_hrw(Cluster::proxy_pool, cat(msg$xid, msg$chaddr), analyze_message, msg, options);
	}

function foo()
	{
	if ( x )
		{
		if ( name in HTTP::proxy_headers )
			{
			add c$http2_streams$streams[stream]$proxied[fmt("%s -> %s",
					name, value)];
			}
		}
	}

function foo()
    {
    if ( test )
        ProtocolAnalyzer::register_packet_handler(ProtocolAnalyzer::ANALYZER_ROOT,  DLT_EN10MB, handler_tag);
    }

event Aaa::bbb_ccccc(ddd: EeeFffff)
	{
	if ( Ggggggg::hh_iiiiiii() && Ggggggg::jjjjj_kkkk_llll() == Ggggggg::MMMMMM )
		Ggggggg::nnnnnnn(Ggggggg::ooooo_ppppp, Aaa::bbb_ccccc_qqqq, ddd);
	}

function foo()
{
if ( test )
{
Aaaaaa::aaa_aaaaa("Aaaaaaaaaaaa::aaaaaaaa_aaaaaa_aaaaaaaaaa", Aaaaaaaaaa::AAA, "Aaaaaaaaaaaa::aaaaaa_aa_aaaaaaaaa_aaaaaaaa");
}
}

function foo()
	{
	xxx_xx_xxxxxxx(fmt("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", Xxxxxxx::xxxx, xxxxxxxxx_xxxxxxxx_xxxxxxxx_xxxxxxx(xxxxxxxx_xxxxxxxx_xxxxxxx)));
	}

function foo()
    {
    Aaa::bbb_cc_ddddddd_eeeeeeeeeeeeeeee("eeeeeeeee.fff_gg_hhh_iii_jjj_kkkkkkkkk.lllllll", mmmmmmm$nnn_oo_ppp_qqq_rrrrrrrrrrr$sssss);
    Aaa::bbb_cc_ddddddd_eeeeeeeeeeeeeeee("eeeeeeeee.fff_gg_hhh_iii_jjj_kkkkkkkkk.tttt",
                                  mmmmmmm$nnn_oo_ppp_qqq_rrrrrrrrrrr$uuuu);
    }

other_function_abc(fmt("tracking %s %s", host, some_uid, #@ NOT-TESTED
            some_dest, extra_data));

NOTICE([$msg=fmt("Value %s was more than %dMB for item %s",
                some_var, some_long_variable_name / 1000 / 1000, bucket)]);
