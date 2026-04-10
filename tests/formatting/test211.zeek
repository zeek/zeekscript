# default with break in switch.
event foo()
    {
    switch auth_type {
    default:
        break;

    case 3: print "yep";
    }
    }
