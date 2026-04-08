type SomeInfo: record {
	## A timestamp field.
	ts: time &log;

	## A unique identifier.
	uid: string &log;

	## A network address.
	addr_field: addr &log;
};
