hook SomeModule::some_log_policy()
	{
	some_code();

	local o_s = rec_id$orig_h/subnet_mask; #@ NO-FORMAT
	local r_s = rec_id$resp_h/subnet_mask; #@ NO-FORMAT

	more_code();
	}
