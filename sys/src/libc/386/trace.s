TEXT _tracein(SB), 1, $0
	CMPL	traceactive(SB), $0
	JEQ	inotready
	MOVL	16(SP),AX
	PUSHL	AX
	MOVL	16(SP),AX
	PUSHL	AX
	MOVL	16(SP),AX
	PUSHL	AX
	MOVL	16(SP),AX
	PUSHL	AX
	MOVL	16(SP),AX
	PUSHL	AX
	CALL	tracein(SB)
	POPL	AX
	POPL	AX
	POPL	AX
	POPL	AX
	POPL	AX
inotready:
	RET

TEXT _traceout(SB), 1, $0
	CMPL	traceactive(SB), $0
	JEQ	notready
	PUSHL	AX
	MOVL	4(SP),AX
	PUSHL	AX
	CALL	traceout(SB)
	POPL	AX
	POPL	AX
notready:
	RET
