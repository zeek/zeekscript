# Comment before else body stays with the else branch.
function foo()
	{
	if ( c?$vpce_id )
		vpce_id = c$vpce_id;
	else
		# No VPC endpoint found, cannot enrich.
		return "";
	}
