enum {
	Anone,
	Amemory,
	Areserved,
	Aacpireclaim,
	Aacpinvs,
	Aunusable,
	Adisable,

	Aapic,
	Apcibar,
	Ammio,
	Alast		= Ammio,
};

enum {
	Mfree,
	Mktext,
	Mkpage,
	Mupage,
	Mvmap,
	Mlast		= Mvmap,
};

uintmem	adralloc(uintmem, uintmem, int, int, int, uint);
void	adrdump(void);
void	adrinit(void);
void	adrmapck(uintmem, uintmem, int, int);
int	adrmapenc(uintmem*, uintmem*, int, int);
void	adrmapinit(uintmem, uintmem, int, int);
uint	adrmemflags(uintmem);
void	adrfree(uintmem base, uintmem len);
int	adrmatch(int, int, int, uintmem*, uintmem*);		/* not currently used */
uintmem	adrmemtype(uintmem, uintmem*, int*, int*);
