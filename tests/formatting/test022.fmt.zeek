event some_evt(si: SomeInfo)
	{
	if ( coal_max_entries > 0 && |coalesced_state| >= coal_max_entries &&
	     [server_name, server_subj, server_issuer,
	      client_subj, client_issuer, ja3] !in
	     coalesced_state )
		return;
	}
