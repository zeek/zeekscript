const some_lookup: table[string] of string = {
    ["some-key-aa"] = "val-aa",
    ["some-key-bb"] = "val-bb",
    ["some-key-cc"] = "val-cc",
    ["some-key-dd"] = "val-dd",
} &default = function(n: string): string
    {
    return fmt("fixme-%s", n);
    };
