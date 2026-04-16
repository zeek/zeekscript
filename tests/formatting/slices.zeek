# Slice expressions: all variants of [start:end] with expressions, empty bounds, and function calls.

function some_func()
	{
	local val = some_long_func_name(data[some$off - 1 + 8 : some$off - 1 + 10 + 23 / 15 - 3]);
	}

xs[  0   :1  ];

xs[  0   :  ];

xs[  : 1 ];

data[1-1:];

data[:1-1];

data[   :   ];

data[1-1:1];

data[1 :1-1];

data[f(): ];
