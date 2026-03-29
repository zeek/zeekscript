function some_func(c: connection): SomeInfo
	{
	local rec = c$some_rec;
	if ( ! rec?$some_field || |rec$some_field| == 0 ||
	     ! rec$some_field[0]?$some_val )
		return ci;
	}
