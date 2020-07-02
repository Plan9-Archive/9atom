#include "x16.h"

TEXT getdriveparm(SB), $0
	MOVL	$0x4800, AX
	MOVL	dev+4(SP), DX
	MOVL	buf+8(SP), SI
	CALL	rmode16(SB)
	CLR(rDI)
	BIOSCALL(0x13)
	CALL16(pmode32(SB))
	JCS fail
	XORL	AX, AX
	RET
fail:
	MOVL	$-1, AX
	RET
