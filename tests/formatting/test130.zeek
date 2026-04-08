event some_evt()
	{
	SOME_CALL([$field_a=Some_Enum_Val,
	           $field_b=fmt("some format string with several placeholders %d items across %d groups within %s window",
	                        val_one, val_two, val_three),
	           $field_c=endpoints, $field_d=src_host, $field_e=cat(src_host)]);
	}
