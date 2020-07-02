/*
 * running the l2 cache as write-back and using cached memory for
 * usb data structures yields spurious errors such as
 *
 *	qhintr: td 0x60ee3d80 csw 0x8824a error 0x48 transaction error
 *
 * from usbehci.  so, at least for now, we will use uncached memory until
 * we sort out the write-back problems.
 */
#define smalloc(n)		ucsalloc(n)
#define free			ucfree
#define xspanalloc		ucallocalign
#define mallocz(n, clr)		ucallocz(n, clr)

#define allocb			ucallocb
#define iallocb			uciallocb
#define freeb			ucfreeb

void*	ucsalloc(usize);
void*	ucallocz(usize, int);
