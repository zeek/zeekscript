# Type expressions
print table  [ int ]   of vector   of    count;

# Large curly braced init should span multiple lines
const machine_types: table[count] of string = { [0x00]   = "UNKNOWN",
       [0x1d3]  = "AM33", [0x8664] = "AMD64", [0x1c0]  = "ARM",
       [0x1c4]  = "ARMNT", [0xaa64] = "ARM64", [0xebc]  = "EBC",
       [0x14c]  = "I386", [0x200]  = "IA64", [0x9041] = "M32R",
       [0x266]  = "MIPS16", [0x366]  = "MIPSFPU", [0x466]  = "MIPSFPU16",
       [0x1f0]  = "POWERPC", [0x1f1]  = "POWERPCFP", [0x166]  = "R4000",
       [0x1a2]  = "SH3", [0x1a3]  = "SH3DSP", [0x1a6]  = "SH4",
       [0x1a8]  = "SH5", [0x1c2]  = "THUMB", [0x169]  = "WCEMIPSV2"
} &default=function(i: count):string { return fmt("unknown-%d", i); };

# Single element curly braced init can go on one line
const thing: table[count] of string = {[123] = "onetwothree"};
