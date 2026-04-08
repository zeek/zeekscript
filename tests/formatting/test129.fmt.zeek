function some_func()
	{
	SOME_CALL([$field_a=Some_Enum_Val,
	           $field_b=fmt("total items: %d, associated ids: %s",
	                        total_items, conn_ids),
	           $field_c=src_host, $field_d=cat(src_host, bucket)]);
	}
