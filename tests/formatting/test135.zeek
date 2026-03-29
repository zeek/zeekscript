event zeek_init()
	{
	Log::create_stream(LOG, [$columns=Conn::Info,
	                $path=path, $policy=Conn::log_policy]);
	}
