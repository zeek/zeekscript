function foo()
	{
	some_handler(opt_id,
	             function[evt_grp](id: string, val: bool): bool
			{
			return val;
			}
	             );
	}
