redef record fa_file += {
	pe: Info &optional;
};

redef enum Log::ID += { LOG };

# Single.
redef record Conn::Info$ip_proto-={&log};
# Multiple (actually only `&log` can be redefined).
redef record Conn::Info$ip_proto-={&log &optional};
