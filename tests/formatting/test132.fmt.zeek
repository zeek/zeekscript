event some_evt(c: connection)
	{
	NOTICE([$note=Some_Notice, $conn=c,
	        # This is a comment about the next field.
	        # Another comment line.
	        $identifier=cat(id$orig_h, id$resp_h)]);
	}
