function foo()
    {
    if ( stream in c$http2_streams$has_data &&
         stream in c$http2_streams$streams )
        {
        Intel::seen([$indicator=HTTP2::build_url(c$http2_streams$streams[stream]),
                     $indicator_type=Intel::URL,
                     $conn=c, $where=HTTP2::IN_URL]);
        }
    }
