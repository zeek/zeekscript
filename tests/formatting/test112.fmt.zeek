function some_fn(h: addr): string
	{
	return some_call(h) ? other_call(h/some_mask) : "fallback"; #@ NO-FORMAT
	}
