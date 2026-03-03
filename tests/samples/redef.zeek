redef record fa_file += {
	pe: Info &optional;
};

redef enum Log::ID += { LOG };

redef enum Log::ID += {
	ONE,
	TWO
};

# Single.
redef record Conn::Info$ip_proto -= { &log } ;
# Multiple (actually only `&log` can be redefined).
redef record Conn::Info$ip_proto -= { &log &optional } ;
