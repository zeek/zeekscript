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
