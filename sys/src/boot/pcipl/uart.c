/* 115200 8ns1 */
extern void outb(int, int);
extern int inb(int);

void
uartinit(void)
{
	outb(0x3f8 + 3, 0x80 | 7);
	outb(0x3f8 + 0, 1);
	outb(0x3f8 + 1, 0);
	outb(0x3f8 + 3, 7);
}

void
uartputc(int c)
{
	int i;

	outb(0x3f8 + 0, c);
	usleep(78);
}

int
uartgotc(void)
{
	if(inb(0x3f8 + 5) & 1)
		return inb(0x3f8 + 0);
	return -1;
}
