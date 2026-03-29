event some_handler(c: connection, is_orig: bool, payload: string) #@ NOT-TESTED
	{
	add c$app["test"];
	}
