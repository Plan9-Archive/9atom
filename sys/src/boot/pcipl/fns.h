/* handy strings in l.s */
extern char origin[];
extern char hex[];
extern char crnl[];
extern char bootname[];

/* l.s */
void start(void *sp);
int kbdgetc(void);
int kbdgotc(void);
void cgaputc(int c);
void uartputc(int);
int uartgotc(void);
void usleep(int t);
void halt(void);
void jump(void *pc);
void reboot(void);
/* cheat */
int inb(int);
void outb(int, int);

/* pxe.c, iso.c, etc. */
char *typeconf(char*);
int read(void *f, void *data, int len);
int readn(void *f, void *data, int len);
void close(void *f);
void unload(void);

/* edd.c */
void bootdrive(void);

void memset(void *p, int v, int n);
void memmove(void *dst, void *src, int n);
int memcmp(void *src, void *dst, int n);
int strlen(char *s);
char *strchr(char *s, int c);
char *strrchr(char *s, int c);
void putc(int);
void print(char *s);
void prle(void*, int);
void confappend(char*, int);
void vaddconf(uvlong, uint);
char *fmtle(char*, void*, int);
char *configure(void *f, char *path);
char *bootkern(void *f);
