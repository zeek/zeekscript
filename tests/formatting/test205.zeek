function foo()
	{
	NOTICE([$note=Some::Note,
		$msg=fmt("%s made %d connections to %s (%s)",
			src_host, total_conns,
			total_bytes, conns),
		$src=src_host,
		$identifier=cat(src_host, bucket)]);
	}
