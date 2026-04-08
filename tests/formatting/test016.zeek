function some_func(c: connection, name: string,
                   value: string, prefix: string)
	{
	if ( name == "some-type" )
		some_handler(c, value, prefix + "-sfx"); #@ NOT-TESTED

	else if ( name == "other-type" )
		other_handler(c, value, prefix + "-sfx");
	}
