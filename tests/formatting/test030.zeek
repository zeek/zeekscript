function some_func(val: string)
	{
	if ( some_pattern == val )
		result = "found";

	# This handles the fallback case.
	# Check secondary pattern too.
	else if ( other_pattern == val )
		result = val[idx + 1 :];
	}
