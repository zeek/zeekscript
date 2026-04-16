# Record and enum types: fields with attributes, doc-comments, trailing comments, one-liner/multi-line enums, fill-enum, redef record/enum.
# test044.zeek
# test045.zeek
# test046.zeek
# test121.zeek
# test131.zeek
# test133.zeek
# test166.zeek
# test175.zeek
# test177.zeek
# test179.zeek
# test184.zeek
# test223.zeek
# test224.zeek
# test262.zeek
# test269.zeek
# test273.zeek
# test274.zeek

export {
type AnomalyTypes: enum {
	ANOMALY, NEW_ENTITY, NEW_ITEM, NEW_ENTITY_NEW_ITEM,
	NEW_ENTITY_ITEM_PAIR, UNKNOWN
};
}

type SomeEnumAA: enum { SE_DNS_A, SE_DNS_AAAA, SE_DNS_A6, SE_DNS_PTR, SE_HTTP_HOST, SE_TLS_SNI, SE_NTLM_AUTH, SE_UNKNOWN, };

type score: enum { NOT = 0, LOW = 1, MED = 2, HIGH = 3 };

type Foo: record {
    aa: string &default="";
    bb: count &default=0; # a comment

    cc: bool;
    dd: bool &default=F;
};

export {
	type SomeAttrs: record {
		some_strings: set[string] &log &optional; # A set of associated strings
	};
}

type SomeInfo: record {
	## A timestamp field.
	ts: time &log;

	## A unique identifier.
	uid: string &log;

	## A network address.
	addr_field: addr &log;
};

export {
	type SomeInfo: record {
		some_field_name: set[string] &log
		                              &optional; # A trailing comment here
	};
}

export {
    redef enum Notice::Type += {
        POTENTIAL_CVE_2022_24497,
    };
}

redef record SSL::Info += {
    # Flag indicating if "early data" extension was sent in Client Hello.
    early_data: bool &default=F;
};

export {
    redef enum Log::ID += { LOG };
}

type CertInfo: record {
    cn: string &log &optional;
    sans: set[string] &log &optional;
};

redef enum Notice::Type += {
    ## Anomalous behavior detected.
    Found
};

export {
        type Flag: enum {
                ControlPkt = 0x80, #< Control messages between tunnel endpoints.
        CriticalOpt = 0x40, #< Geneve header contains a critical option.
    } &redef;
}

type Foo: record {  x: count;  y: string; } &redef;

export {
    ## Documentation comment.
    type Foo: enum {
        AAA, ##< Trailing comment on AAA
        BBB ##< Trailing comment on BBB
    };
}

export {
        redef record Aaa::Aaaa += {
                aaaa_aaa: count &default=0;

        # aaaaaaa_aaaaa: string &log &optional;
    };
}

export {
    type foo: enum {
        AAAAA, BBBBB, CCCCC, DDDDD, EEEEE, FFFFF, GGGGG, HHHHH,
        IIIII, JJJJJ, KKKKK,

        LLLLL,
    };
}

export {
    #@ FORMAT: FILL-ENUM
    type Foo: enum {
        AAAA, BBBB, CCCC, DDDD, EEEE, FFFF, GGGG, HHHH,
        IIII, JJJJ, KKKK, LLLL, # trailing
        MMMM,

        NNNN, OOOO,
    };
}
