event conn_event(c: connection, is_orig: bool, name: string, value: string)
    {
    if (/.{0,20}aaaa_aaaa\(aaaa_aaaa|aaaa%28aaaa_aaaa.{0,20}/ in value ||
            /.{0,20}aaaa_aaaa\(aaaa_aaaa|aaaa%28aaaa_aaaa.{0,20}/ in value ||
            /.{0,20}\.\.\/\.\.\/\.\.\/\.\.\/\.\..{0,20}/ in value ||
            /.{0,20}\.\.%c0%af\.\.%c0%af/ in value ||
            /.{0,20}aaa\/aaaaaa|aaa\%c0\%afaaaaaa|aaa\%c0\%2faaaaaa.{0,20}/ in value ||
            /.{0,20}aaa\/aaaaaa|aaa\%c0\%afaaaaaa|aaa\%c0\%2faaaaaa.{0,20}/ in value ||
            /.{0,20}\\r\\n.{0,20}/ in value ||
            /.{0,20}\\x0d.{0,20}/ in value ||
            /.{0,20}\\x0a.{0,20}/ in value ||
            /.{0,20}\<aaaaaa\>.{0,20}/ in value ||
            /.{0,20}\<aaaa\>.{0,20}/ in value ||
            /.{0,20}\/aaaa\.aaa.{0,20}/ in value ||
            /.{0,20}aaaaaaa\/aaa\.aaa.{0,20}/ in value ||
            /.{0,20}AAAAAA\ AAAA|AAAAAA%20AAAA.{0,20}/ in value ||
            /.{0,20}AAAAAA\ \*\ AAAA|AAAAAA%20%2A%20AAAA.{0,20}/ in value ||
            /.{0,20}AAA\ AAAA|AAA%20AAAA.{0,20}/ in value ||
            /.{0,20}\'\ or\ 1.{0,20}/ in value ||
            /.{0,20}AAAAAA\ .{0,20}/ in value)
        {
        add c$http$inferences["ABC"];
        }
    }
