function foo()
    {
    # Create stream for our alternative, coalesced files.log-like log.
    Log::create_stream(LOG, [$columns=Info, $path=path, $policy=log_policy]);
    }
