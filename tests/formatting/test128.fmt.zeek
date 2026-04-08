event zeek_init()
	{
	Some::create_stream(SOME::LOG, [$columns=Info, $path="some_path",
	                                $policy=log_policy]);
	}
