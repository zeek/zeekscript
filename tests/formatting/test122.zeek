event zeek_init()
	{
	Log::create_stream(SOME_LOG,
		[$columns=SomeInfo, $path="some_log_path", $policy=some_log_policy]);
	}
