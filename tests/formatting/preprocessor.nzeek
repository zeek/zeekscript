# Preprocessor directives: @if/@else/@endif, @ifdef/@ifndef, @load, nesting inside function bodies, with trailing comments and annotations.
# test028.zeek
# test029.zeek
# test031.zeek
# test066.zeek
# test112.zeek
# test113.zeek
# test114.zeek
# test115.zeek
# test116.zeek
# test182.zeek

# Some comment.
#@ BEGIN-SKIP-TESTING
@if ( some_func("/some/path") > 0 )
    @load /some/path
@else
    @load packages/some-pkg
@endif
#@ END-SKIP-TESTING

@ifdef ( SOME_FEATURE )

    event zeek_init()
        {
        print some_var;
        }

    # Fallback path below.
@else

    event zeek_init()
        {
        print "other";
        }

@endif

@ifdef ( SOME_FEATURE )

    event zeek_init()
        {
        print some_var;
        }

    # Note: This comment belongs inside the ifdef block.
    # It should be tab-indented.
@endif

@ifdef ( SOME_FEATURE )
event SomeModule::some_esp_message(c: connection, is_orig: bool, msg: SomeModule::SomeEspMsg) &group=MyPkg_SomeEvtGroup { shuntit(c); }
@endif

module SomeMod;

@load ./const

@if ( FOO )
@load foo
@else
@load bar
@endif

@ifdef ( SomeFeature )

    module SomeMod;

    const some_var = "some-value";

    event zeek_init()
        {
        print some_var;
        }
@endif

@ifndef ( Some::validated_items ) #@ FALSE-NOT-TESTED
    @load policy/protocols/some/validate-items
@endif

function foo()
	{
@if ( FOO )
	print 1;
@endif
	}

@if ( FOO )
print 1;
@endif
