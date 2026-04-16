# Miscellaneous statements: add, delete, assert, hook, when/timeout, schedule, copy, ++/--, event statement, return, print, bare block, #@ BEGIN marker, #@ BEGIN-SKIP-TESTING, #@ END comment on else.

#@ BEGIN-SKIP-TESTING

function some_func(val: string)
	{
	some_call(val);
	}

#@ END-SKIP-TESTING

event some_handler(rec: SomeModule::Info)
	{
	add some_long_cache[aaa, bbb, ccc, rec$source_field, rec$type_field, rec$name_field, rec$orig_flag, rec$byte_count];
	}

function some_func(a: count, b: string, rec: SomeRec)
	{
	#@ BEGIN-SKIP-TESTING
	if ( rec in did_check )
		return;
	#@ END-SKIP-TESTING

	next_thing();
	}

function f() { schedule some_time_interval { SomeModule::some_very_long_event_name() }; }

#@ BEGIN
function foo()
    {
    bar();
    }
#@ END

event foo()
	{
	hook my_hook(x, y);

	when ( some_cond() )
		{
		print "yes";
		}

	when ( local result = do_lookup(host) )
		{
		print result;
		}
	timeout 10 secs
		{
		print "timeout";
		}
	}

event foo()
	{
	local x = copy(some_val);
	++x;
	--x;
	}

event Aaaaaaaaaaaa::xxxxxx_xxxx_xx_xx_xxxxxx_xxx(xx_xxxxxxx: set[addr, bool, bool]) &is_used #@ NOT-TESTED
{ if ( Aaaaaaa::xxxx != Aaaaaaaaaaaa::xxxxxxxxxxxxx ) { xxxxxxxxxxxxxx(xxx("xxxxx")); } }

event aaaa()
{
if ( test )
{
print "aaaa";
}
#@ END Aaaa::aaaa
else
{
print "bbbb";
}
}

function foo()
	{
	schedule 10 secs { Xxxxxxxxxxxx::xxx_xxxxxxx_xxxxx_xxxxxxxxx_xxx_xxxxxxxxx() };
	}

function foo()
	{
	schedule xxxx_xxxxxx_xxxx_xxxxxxxx { XxxxXxxxxxxXxxxXxxxx::xxxx_xxxx_xxxxx_xxxx_xxxx(xxxxxxxx_xxx, xxxxxxxx_xxx, xxxxxxxx_xxxxx) };
	}

event foo()
    {
    event bar();
    event baz(1, 2, 3);
    }

function foo()
    {
    when [bar] ( x ) print bar;
    when [bar, baz] ( x )  { print bar; }
    }

# A final comment.
