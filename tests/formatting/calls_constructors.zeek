# Constructor calls: record constructors ($field=), Log::write, Log::create_stream, NOTICE, Input::add_event, nested constructors with fmt().

function some_func(): SomeModule::SomeType
	{
	if ( some_condition && some_other_condition )
		some_long_variable_name[some_id] =
			SomeModule::SomeType($aa=result$aa,
			                     $bb_string=result$bb_string);
	}

event zeek_init() { SomeModule::some_register_fn(SomeModule::SOME_ANALYZER, [$get_handle=SomeModule::get_handle, $describe=SomeModule::describe_it]); }

event foo() { SomeModule::seen([$some_field=SomeModule::build_value(c$some_data$items[stream]), $some_type=SomeModule::URL, $conn=c, $where=SomeModule::IN_URL]); }

function some_func()
    {
    if ( test )
        {
        local some_val =
               some_long_func_name(result_chain[1], 4); # HASH
        }
    }

event zeek_init() { local filter = Log::Filter($name="log-name", $path="log_path", $include=set("id.orig_h", "id.orig_p", "id.resp_h", "id.resp_p", "app"), $policy=some_policy_fn); }

function some_fn()
    {
    if ( some_condition )
        {
        local info = SomeLongModule::SomeFn($ts=ts, $uid=uid,
            $use_case=usecase,
            $use_case_description=usecase_desc,
            $entity_training_items=entity_training_items,
            $entity=entity, $original_entity=original_entity,
            $item=item,
            $first_seen_type=some_enum_map[some_type],
            $history_days=history_days, $history=history);
        }
    }

function some_fn()
    {
    local info = SomeModule::SomeFn($note=Found, $uid=uid,
            $msg=fmt("%s found on %s entity using %s item.",
                usecase_desc, orig_entity, item),
            $sub=fmt("Score: %s. Days: %s",
                item_score, history_days),
            $identifier=cat(orig_entity, usecase, item));
    }

event zeek_init()
	{
	Log::create_stream(SOME_LOG,
		[$columns=SomeInfo, $path="some_log_path", $policy=some_log_policy]);
	}

event some_evt()
    {
    SomeModule::setup_stream(SOME_STREAM_LOG, [$columns=SomeStreamInfo,
                                               $path="some_long_module_stream_path",
                                               $policy=some_stream_log_policy
                       ]);
    }

event some_evt(c: connection)
	{
	NOTICE([$note=Some_Notice,
		$conn=c,
		# This is a comment about the next field.
		# Another comment line.
		$identifier=cat(id$orig_h, id$resp_h)]);
	}

event some_evt()
    {
    some_func([$aa=BB,
              $cc=fmt("Host uses a weak certificate with %d bits",
                      key_len),
              $dd=e, $ff=SSL::some_suppression_interval,
              $gg=cat(resp_h, c$id$resp_p, hash, key_len)]);
    }

event some_event()
	{
	some_func([$aa=BB, $cc=dd, #@ SOME-TAG
	           $ee=ff]);
	}

event zeek_init()
	{
	Log::create_stream(LOG, [$columns=Conn::Info,
	                $path=path, $policy=Conn::log_policy]);
	}

event zeek_init()
	{
	Some::create_stream(SOME::LOG, [$columns=Info, $path="some_path",
	                               $policy=log_policy]);
	}

function some_func()
	{
	SOME_CALL([$field_a=Some_Enum_Val,
	           $field_b=fmt("total items: %d, associated ids: %s",
	                        total_items, conn_ids),
	           $field_c=src_host, $field_d=cat(src_host, bucket)]);
	}

event some_evt()
	{
	SOME_CALL([$field_a=Some_Enum_Val,
	           $field_b=fmt("some format string with several placeholders %d items across %d groups within %s window",
	                        val_one, val_two, val_three),
	           $field_c=endpoints, $field_d=src_host, $field_e=cat(src_host)]);
	}

function f() {
	if ( x )
		Input::add_event([$source=some_config_source_setting, $name=some_config_name_setting, $fields=SomeConfigRecord, $mode=Input::REREAD, $want_record=F, $ev=some_config_event_added]);
}

function foo()
    {
        Log::write(VPN_TYPE_LOG,
                   VPNType($date=date, $orig_subnet=o_s, $resp_subnet=r_s,
                           $proto=proto_str, $vpn_type=vtype_str, $uid=rec$uid));
    }

function foo()
    {
    Log::write(NS_LOG, NS_ServersService($date=date, $ip_mask=ip_mask,
                                         $resp_p=resp_p, $proto=p,
                                         $service=corrected_services,
                                         $is_server=is_server, $uid=c$uid));
    }

function foo()
    {
    Log::write(EW_LOG,
               EW_ServersService($date=date, $orig_subnet=o_s,
                                 $resp_subnet=r_s, $service=conn$service,
                                 $uid=c$uid));
    }

function foo()
    {
    Log::write(FTP_LOG,
               FTPConnections($date=date, $orig_subnet=orig_subnet,
                              $resp_subnet=resp_subnet, $uid=c$uid));
    }

function some_func(src_host: addr, services: int,
                           regions: int, endpoints: string)
    {
    NOTICE([$note=Some_Notice_Type,
            $msg=fmt("Some long format string describing something with multiple parameters (%d items across %d groups within the last %s).",
                     services, regions, some_window),
            $sub=endpoints, $src=src_host, $identifier=cat(src_host)]);
    }

function foo()
	{
	NOTICE([$note=Some::Note,
		$msg=fmt("%s made %d connections to %s (%s)",
			src_host, total_conns,
			total_bytes, conns),
		$src=src_host,
		$identifier=cat(src_host, bucket)]);
	}

function foo()
    {
    local u = Bar($aaa=table() &create_expire=window,
                          $bbb=table() &create_expire=window,
                          $ccc=notice);
    }

function foo()
    {
    if ( ssl_cache_intermediate_ca && issuer_name_hash in intermediate_cache )
        {
        Intel::seen([$indicator=HTTP2::build_url(c$http2_streams$streams[stream]),
                     $indicator_type=Intel::URL,
                     $conn=c, $where=HTTP2::IN_URL]);
        }
    }

function foo()
    {
    if ( test )
        {
        NOTICE([$note=Weak_Cipher,
            $msg=fmt("Host established connection using unsafe ciper suite %s",
                 c$ssl$cipher),
            $conn=c, $suppress_for=SSL::notice_suppression_interval,
            $identifier=cat(resp_h, c$id$resp_p, c$ssl$cipher)]);
        }
    }

function foo()
    {
    # Create stream for our alternative, coalesced files.log-like log.
    Log::create_stream(LOG, [$columns=Info, $path=path, $policy=log_policy]);
    }

function xxx()
	{
	if ( x )
		{
		i$xxxxxxxxxxxxxxxxxxxxxxxxx =
			xxxxxxxxxxxxxxx(i$xxxxxxxxxxxxxxxxxxxx,
					|xxxxxxx| + |xx|);
		i$xxxxxxxxxxxxxxxxxxxxxx = +|xxxxxxx| - |xx|;
		}
	}

function foo()
	{
	Log::write(SSH_CONN_LOG, SSHConnections($date=date, $orig_subnet=orig_subnet, $resp_subnet=resp_subnet, $uid=rec$uid));
	}

function foo()
    {
    NOTICE([$aaa=Xxxxxxxxx::XXXX, $bbb=c, $ccc="Potential Responder/Pretender MitM HTTP server headers detected. Refer https://attack.mitre.org/software/S0174/"]);
    }

event foo() {
local i = DebugInfo($t=network_time(), $node_type=node_type(), $event_type="log_statistics", $msg=fmt("stepping-stones-v%d: cn=%s tps=%d trs=%d ips=%d irs=%d", statistics_info$interval_removes_seen));
}

some_table_variable_name[some_key] = X509::Result($result=result$result, $result_string=result$result_string);

local x = [$msg=fmt("Host uses protocol version %s which is lower than the safe minimum %s", some_string, other_string_a)];

local filter = Log::Filter($name="log-name", $path="log_path", $include=set("id.orig_h", "id.orig_p", "id.resp_h", "id.resp_p", "app"), $policy=some_policy_fn);

local filter = Log::Filter($name="log-name", $path="log_path",
                               $include=set("id.orig_h", "id.orig_p",
                                            "id.resp_h", "id.resp_p",
                                            "app"), $policy=some_policy_fn);
