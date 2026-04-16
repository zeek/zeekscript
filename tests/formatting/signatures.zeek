# Function/event signatures: formal arg wrapping, &group, &is_used, nested function-type params, semicolon separator, global function types.

event some_handler(c: connection, is_orig: bool,
                   payload: string) #@ NOT-TESTED
	{
	add c$app["test"];
	}

event some_event(some_arg: string)
	{
	local some_name = some_func(arg_one, arg_two, arg_three);
	}

event SomeModule::some_raised_evt(c: connection, is_orig: bool, version: count, some_dcid: string, some_scid: string, some_retoken: string, some_integ_tag: string) &group=SomeEvtGroup { print c; }

event foo(c: connection) &group=bar { print c; }

event some_generic_packet_threshold_crossed(c: connection, threshold: count) &group=MyPkg_SomeUnknownprotos_EvtGroup { print c; }

event ssl_extension(c: connection, is_client: bool, code: count, val: string) &group="doh-generic"
	{
	}

event some_encrypted_data(c: connection, is_orig: bool, some_record_vers: count, some_content_t: count, length: count) &group="some-group" { }

event some_handler(aa: connection, bb: bool, cc: count, dd: string,
                   ee: string, ff: string, gg: string,
                   hh: string, ii: string, jj: bool)
	{
	}

event SomeModule::Geneve::some_filtered_option(inner_c: connection,
	inner_hdr: pkt_hdr, vni: count,
	flags: count,
	opt: SomeModule::Geneve::some_geneve_hdr_option)
	{
	}

event Some::handler(c: connection, is_orig: bool, name: string,
                   value: string) #@ NOT-TESTED
    {
    }

event some_rpc_handler(aa: connection, bbb: count, cccc: count, ddd: string, eee_major: count, fff_minor: count) #@ NOT-TESTED
	{ }

function register_use_case(use_case: string,
                           notice_lambda: function(o_h: addr,
                                                   si: ProxySourceInfo),
                  volume_threshold: double &default=default_volume_threshold)
    {
    }

export {
    global observation_result:
        function(o: string, label: string, conf: Confidence::level,
             source: string, caller: string,
             cache: bool &default=F);
}

export {
    global has_text_runs: function(data: string, length: count,
                                       run_min: count, runs_min: count): bool;
}

event Aaaaaaaaaaaa::xxxxxx_xxxx_xx_xx_xxxxxx_xxx(xx_xxxxxxx: set[addr, bool, bool]) &is_used #@ NOT-TESTED
{ if ( Aaaaaaa::xxxx != Aaaaaaaaaaaa::xxxxxxxxxxxxx ) { xxxxxxxxxxxxxx(xxx("[AAAAAA_AAAAAAA] Aaaaaaaa aaaaaaa aaaaa aaaa %s -> %s in %s", "update_addr_to_vt_counts_evt")); } }

event aaa_aaaaaaaaa_aaaaaa_aa_aaa(aaa: Input::AAAAAAAAAAAAaaaa,
                                  aa: Input::Aaaaa, AAAA: subnet)
    {
    }

function aaa_bbb_ccc_ddd_eeeee(
    ff: event(ggg_hhh_iiis: vector of time, jjj_kkkk: vector of
        string; lll_mmm: vector of conn_id, nnn_ooo_pp: time,
        qqqqqqqqq: string, rrrrr_ssssss: string, tttt: string,
        uuuuuu: string, vvvv_wwww: string, xxx: string, yyy: string,
        zzzzzz: string),
    zzzzzz: string)
    {
    }

export {
    global aaaaaaaaa_aa: function(t: table[addr, addr] of set[time, string],
                    aaaa_a: addr, aaaa_a: addr): interval;
};
