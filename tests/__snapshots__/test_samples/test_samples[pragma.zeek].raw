event run_sync_hook()
	{
	hook Telemetry::sync();
@pragma push ignore-deprecations
	schedule sync_interval { run_sync_hook() };
@pragma pop ignore-deprecations
	}
