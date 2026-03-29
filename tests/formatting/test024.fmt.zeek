event some_handler(rec: SomeModule::Info)
	{
	if ( [aaa, bbb, ccc, rec$source_field, rec$type_field,
	      rec$name_field, rec$orig_flag, rec$byte_count] in
	     some_long_cache_name )
		return;
	}
