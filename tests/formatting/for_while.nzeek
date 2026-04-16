# For/while loops: single/multiple loop vars, nested for, trailing comments, long while conditions, empty while body, while in if/else.
# test011.zeek
# test060.zeek
# test117.zeek
# test154.zeek
# test158.zeek
# test163.zeek
# test260.zeek
# test267.zeek

function foo()
	{
	for ( x in xs )
		for ( y in ys )
			some_long_table[some_key] =
					SomeFunc($field_a=val_a, $field_b=val_b);
	}

function f() { for ( idx in result$matches ) for ( conn_idx in result$some_uids_with_a_very_long_field_name_that_forces_wrapping ) local x = 1; }

function f() { for ( a, b in tbl ) print file_handle, some_variable, another_table[some_index], final_value; }

event some_evt()
	{
	for ( idx in some_list ) # iterate items
		print idx;
	}

event some_evt()
	{
	for (val in some_set)
		next; # skip it
	}

event some_evt()
	{
	while ( some_flag ) # keep going
		print 1;
	}

function foo()
	{
	if ( test )
		{
		while ( cond )
			{
			if ( cond2 )
				stmt;
			else
				break;
			}
		}
	}

function foo()
    {
    while ( aaaa_aaaaa_aaaaaa < 200 && aaaa_aaaaaaa == 0 &&
        a < a - 5 && a < 30 )
        ;
    }
