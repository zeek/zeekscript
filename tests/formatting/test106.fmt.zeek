global some_tracking_var: table[count, string] of TrackingRec
                           &default_insert = TrackingRec($field=val)
                           &create_expire = 10 sec
                           &redef;
