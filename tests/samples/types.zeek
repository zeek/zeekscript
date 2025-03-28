const foo = {[2620:83:8000:140::3], [2620:83:8000:140::2], 127.0.0.1};

# Enum with deprecated fields should expand to different lines
type enum_with_deprecated: enum { ZERO, ONE &deprecated, TWO &deprecated, };

# Enum with deprecated fields should expand to different lines
type rec_with_optional: record { one: count &optional; two: count &optional; };
