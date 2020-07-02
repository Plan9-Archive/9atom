/*
 *	tas uses LDSTUB
 */
	TEXT	_tas(SB), 1, $-4

	TAS	(R7),R7
	RETURN
