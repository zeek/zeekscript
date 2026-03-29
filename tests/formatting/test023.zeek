function some_function_name_aa(c: connection): string
    {
    if ( c?$tunnel && |c$tunnel| >= 2 &&
         c$tunnel[|c$tunnel| - 1]$tunnel_type == Tunnel::VXLAN )
        print "yep";
    }