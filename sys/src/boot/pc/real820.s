/*
 * do e820 scan before we switch to protected mode.
 * the loader is really getting in the way here.
 */

e820:
	OPSIZE; BYTE $0x1e			/*	pushl ds			*/
	OPSIZE			/* 66 */		/*	.code16			*/
	XORL	BX, BX				/* 	xorl 	ebx, ebx		*/
	OPSIZE; BYTE $0x8e; BYTE $0xdb	/* 	movl 	ebx, ds		*/
	OPSIZE; MOVL	$pe820end, AX		/*	movl 	$pe820end, eax	*/
	BYTE $0x67; BYTE $0x66	/**/
	MOVL	BX, 0(AX)			/*	movl	bx, 0(eax)	*/
	LWI(pe820tab, rDI)	/* 31 db bf */	/*	mov	$pe820tab, di	*/
_1b:						/*1:				*/
	OPSIZE			/* 66 */
	MOVL	$0xe820, AX	/* b8 20 e8 0 0 */	/*	movl	$0xe820, ax	*/
	OPSIZE
	MOVL	$smap, DX			/*	movl	$smap, edx	*/
	LWI(20, rCX)		/* 14 00 */	/*	mov	$20, cx		*/
	OPSIZE; BYTE $0x1e	/* 66 1e */	/*	pushl	ds		*/
	OPSIZE; BYTE $0x07	/* 66 07 */	/*	popl	es		*/
	INT	$0x15		/* cd 15 */	/*	int	$0x15		*/
	JC	_1f		/* 72 xx */	/*	jc	1f		*/
	OPSIZE
	CMPL	AX, $smap	/* 66 3d PMAS */	/*	cmpl	$smap, eax	*/
	JNE	_1f		/* 75 xx */
/*	INCB	(e820end) */			/*	incb	e820end		*/
	BYTE	$0xfe; BYTE $0x06; BYTE $(pe820end & 0xff); BYTE $(pe820end >>8)
	OPSIZE
	ADDL	$e820sz, DI	/* 66 83 c7 14 */	/*	addl	$e820sz, di	*/
	OPSIZE			/* 66 */
	CMPL	DI, $pe820end	/* 81 ff 10 04 */	/*	cmpl	di, pe820end	*/
	JE	_1f		/* 74 xx (06)	/*	je	1f		*/

	OPSIZE			/* 66 */
	CMPL	BX, $0		/* 83 fb 00	*	cmpl	$0, ebx		*/
	JNE	_1b				/*	jne	1b		*/

_1f:
	BYTE $0x66; BYTE $0x1f			/*	popl	ds	*/
