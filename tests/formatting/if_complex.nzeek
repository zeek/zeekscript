# Complex if/else: long &&/|| chains, nested ifs, multi-line conditions, comment before else, @ifdef inside if, empty-statement bodies, else-if chains.
# test004.zeek
# test009.zeek
# test016.zeek
# test017.zeek
# test019.zeek
# test020.zeek
# test021.zeek
# test022.zeek
# test030.zeek
# test043.zeek
# test065.zeek
# test107.zeek
# test172.zeek
# test174.zeek
# test187.zeek
# test199.zeek
# test216.zeek
# test217.zeek
# test218.zeek
# test232.zeek
# test233.zeek
# test235.zeek
# test236.zeek
# test237.zeek
# test241.zeek
# test242.zeek
# test247.zeek
# test270.zeek
# test279.zeek

event some_event(some_arg: string)
	{
	if ( T )
		{
		if ( T )
			{
			local some_long_name = SomeModule::some_function(some_really_long_argument);
			}
		}
	}

function some_func(c: connection, name: string,
                   value: string, prefix: string)
	{
	if ( name == "some-type" )
		some_handler(c, value, prefix + "-sfx"); #@ NOT-TESTED

	else if ( name == "other-type" )
		other_handler(c, value, prefix + "-sfx");
	}

function some_func(val: string)
	{
	if ( some_pattern == val )
		result = "found";

	else
		result = "default";
	}

event zeek_init()
	{
	if ( alpha_val > threshold && beta_val > threshold && gamma_val > threshold && delta_val > threshold && epsilon_val > threshold && zeta_val > threshold )
		print "yes";
	}

event some_evt(si: SomeInfo)
	{
	if ( coal_max_entries > 0 && |coalesced_state| >= coal_max_entries &&
	     [server_name, server_subj, server_issuer, client_subj,
	      client_issuer, ja3] !in coalesced_state )
		return;
	}

function some_function_name_aa(c: connection): string
    {
    if ( c?$tunnel && |c$tunnel| >= 2 &&
         c$tunnel[|c$tunnel| - 1]$tunnel_type == Tunnel::VXLAN )
        print "yep";
    }

event some_handler(rec: SomeModule::Info)
	{
	if ( [aaa, bbb, ccc, rec$source_field, rec$type_field, rec$name_field,
	      rec$orig_flag, rec$byte_count] in some_long_cache_name )
		return;
	}

function some_func(val: string)
	{
	if ( some_pattern == val )
		result = "found";

	# This handles the fallback case.
	# Check secondary pattern too.
	else if ( other_pattern == val )
		result = val[idx + 1 :];
	}

function some_handler(c: connection)
    {
    if ( foo )
        {
        if ( bar )
            {
            if ( bletch )
                other_function_abc("Some kind of status message"); #@ NOT-TESTED
            }
        }
    }

function some_func(c: connection): SomeInfo
	{
	local rec = c$some_rec;
	if ( ! rec?$some_field || |rec$some_field| == 0 ||
	     ! rec$some_field[0]?$some_val )
		return ci;
	}

function some_func(val: string)
	{
	if ( some_pattern == val )
		result = "found";
	else
		result = "default";
	}

function f()
	{
	if ( x )
		# comment before body
		print "done";
	}

event test_fn() {
    if ( some_condition ) {
        if ( [date, ip_mask, resp_p, p, corrected_services, is_server] in ns_svc_cache )
            return;
    }
}

# Comment before else body stays with the else branch.
function foo()
	{
	if ( c?$vpce_id )
		vpce_id = c$vpce_id;
	else
		# No VPC endpoint found, cannot enrich.
		return "";
	}

event zeek_init()
	{
	if ( alpha_val > threshold && beta_val > threshold && gamma_val > threshold && delta_val > threshold )
		print "yes";
	}

function foo()
    {
    if ( ssl_cache_intermediate_ca && result$result_string == "ok" && result?$chain_certs && |result$chain_certs| > 2 )
        bar();
    }

function foo()
    {
    if ( stream in c$http2_streams$has_data &&
         stream in c$http2_streams$streams )
        {
        Intel::seen([$indicator=HTTP2::build_url(c$http2_streams$streams[stream]),
                     $indicator_type=Intel::URL,
                     $conn=c, $where=HTTP2::IN_URL]);
        }
    }

function aaa_bbb_ccc_ddd_eee(c: connection, is_orig: bool) {
if ( c$aaa?$bbb && c$aaa?$ccc && (/\.ddd$/ in c$aaa$eee || /\.fff$/ in c$aaa$eee) ) {
if ( (c$aaa$bbb == "GGG" && c$aaa$hhh_iii in jjj_kk_lll_mmmmmmmmm) || (c$aaa$bbb == "NNN" && c$aaa$hhh_iii in jjj_kk_ooo_mmmmmmmmm) ) {
ppp_qqqqqqq = "Aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa. Aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa aaaa.  (Aaaa 1)";
}
}
}

event aaa() {
if ( bbb ) {
if ( ccccc_ddddd_eeeeeeeeee == 1 && fffff[g]$hhhh == "IIIII-JJJJJJJ" && fffff[g]$kkkkk == /ll-mmmmm, nn-ooooo, pppp-qqqqqqqqqq/i )
ccccc_ddddd_eeeeeeeeee = 2;
}
}

event aaa_bbbbbbb(a: aaaaaaaaaa, bbb_ccc: bool, dddd: string, eeeee: string)
	{
	if ( (a$bbbb$aaaaaa_bbbbbbbbb_cccccc_ddddd1 == 2 && a$bbbb$aaaaaa_bbbbbbbbb_cccccc_ddddd2 == 2) || ! a$bbbb?$ffffff || a$bbbb$ffffff != "AAAA" )
		{
		return ;  #@ NOT-TESTED
		}
	}

function foo()
    {
    if ( test1 )
        {
    @ifdef ( bar )
        return; #@ NOT-TESTED
    @endif
        }
    }

function foo()
    {
    if ( test1 )
        {
        }
    else # aaaa bb bbb cccc
        {
        bar();
        }
    }

function xxx()
	{
	if ( xxx )
		;
	else if ( xxxxxxxxxxx$xxx_xxx > 0 && xxxxxxxxxxx$xxx_xxx - 1 == xxxxxxx$xxx_xxx )
		;
	}

function xxx()
	{
	if ( xxx )
		;
	else if ( xxxxxxxxxxx$xxx_xxx > 0 && xxxxxxxxxxx$xxx_xxx - 1 == xxxxxxx$xxx_xxx )
		;
	else
		;
	}

function xxx()
	{
	if ( x )
		{
		if ( x )
			{
			Xxx::xxxxxxxxx("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", X, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
			Xxx::xxxxxxxxx("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", Xxxxxxxxxx::XXX, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
			}
		}
	}

function foo()
    {
    if ( aaa )
        if ( aaa )
            if ( aaa )
                               add aaa$aaaaaaaaaa[aa > 1.0 sec ? "AAA" : "AAA"];
    }

function foo()
    {
        if ( aaa in bbb && ccc == ddd[aaa] )
        # eee fff ggg
                ; #@ NOT-TESTED
        else if ( /^hhh/ in aaa && iii == 443/tcp )
        # jjj - kkk lll mmm nnn ooo ppp 2 qqq
                ; #@ NOT-TESTED
        else
                rrr();
    }

if ( condition )
    {
    # Fill this in sometime
    }

if ( age >= 1 day )
            age_d = interval_to_double(network_time() -
                                       cert$not_valid_before) /
                    86400.0;
