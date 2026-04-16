# Comments: trailing on various statement types, pre-comments, doc-comments, comment-only file, comment after close, #@ annotations, bare block with comment, pattern table with interleaved comments.

event zeek_init() {}
# Comment on next line.

event some_evt()
	{
	add some_set[val]; # track it
	delete some_set[val]; # remove it
	}

event some_evt()
	{
	assert 1 == 1; # sanity
	}

function some_func(a: count, b: string, rec: SomeRec)
	{
	if ( rec in did_check ) #@ BEGIN-SKIP-TESTING
		return;
	}

event some_evt()
	{
	local x = 1; # init
	}

module SomeMod; # main module

event some_evt()
	{ # Start tracking.
	print 1;
	}

function some_func(): count
	{
	return 42; # the answer
	}

const service_patterns = table(
    [/(.*\.)?(akamaized|akamaihd|akamai|akamaiapis)\.(com|net)/i] =
        "akamai",
    # Meta domains.
    [/(.*\.)?(meta|(oculus(cdn)?)|facebook(-dns|-hardware)?|fbcdn)\.(com|net)/i] =
        "facebook",
);

const service_patterns = table(
    [/(.*\.)?(akamaized|akamaihd|akamai|akamaiapis)\.(com|net)/i] =
        "akamai",

    # Meta domains.
    [/(.*\.)?(meta|(oculus(cdn)?)|facebook(-dns|-hardware)?|fbcdn)\.(com|net)/i] =
        "facebook",
);

const some_patterns = table(
    [/aaa/i] = "aaa",
    # Group end

    [/bbb/i] = "bbb",
);

function foo()
	{
	{
	# a comment
	}
	}

# Trailing comment on last element before closing paren.
redef login_timeouts = set(
    "timed out",
    "Timeout",
    "Timed out", # Comment 1
    "Error reading command input", # Comment 2
);

print "hello"; # a note

;
