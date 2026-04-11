function foo()
    {
    if ( test )
        {
        NOTICE([$note=Weak_Cipher,
            $msg=fmt("Host established connection using unsafe ciper suite %s",
                 c$ssl$cipher),
            $conn=c, $suppress_for=SSL::notice_suppression_interval,
            $identifier=cat(resp_h, c$id$resp_p, c$ssl$cipher)]);
        }
    }
