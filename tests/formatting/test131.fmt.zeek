event some_evt()
	{
	SomeModule::setup_stream(SOME_STREAM_LOG,
	                         [$columns=SomeStreamInfo,
	                          $path="some_long_module_stream_path",
	                          $policy=some_stream_log_policy]);
	}
