@ifdef ( SOME_FEATURE )

    event zeek_init()
        {
        print some_var;
        }

    # Note: This comment belongs inside the ifdef block.
    # It should be tab-indented.
@endif
