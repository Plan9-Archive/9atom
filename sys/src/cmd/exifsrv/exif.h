
enum {
	TEM = 0x01,
	SOF = 0xC0,
	DHT = 0xC4,
	JPGA = 0xC8,
	DAC = 0xCC,
	RST = 0xD0,
	SOI = 0xD8,
	EOI = 0xD9,
	SOS = 0xDA,
	DQT = 0xDB,
	DNL = 0xDC,
	DRI = 0xDD,
	DHP = 0xDE,
	EXP = 0xDF,
	APP = 0xE0,
	JPG = 0xF0,
	COM = 0xFE,

	JFXX_jpeg = 0x10,

	EXIF = 0x8769,
	EX_toff = 0x0201,
	EX_tlen = 0x0202,
};

typedef struct {
	vlong toff;		// thumbnail offset
	long tlen;		// thumbnail length
	char *mdata;		// metadata buffer
	int mlen;		// metadata length
	Fmt mfmt;		// metadata format string

	char *file;		// physical file accessed
	char mode;		// f/t/m indicating fullsize/thumbnail/metadata
	Biobuf *bp;		// input file
	int intel;		// file is big endian
} Img;	


typedef struct Namval Namval;
struct Namval {
	int val;
	char *name;
};

typedef struct Exif Exif;
struct Exif {
	int useful;
	int tag;
	char *name;
	void (*func)(Img *ip, int base, Exif *, int fmt, int num, int val);
	Namval *nv;
};

extern Exif Table[];

extern void tag_other(Img *, int, Exif *, int, int, int);
extern void tag_shutter(Img *, int, Exif *, int, int, int);
extern void tag_version(Img *, int, Exif *, int, int, int);
extern void tag_comment(Img *, int, Exif *, int, int, int);
extern void tag_apex(Img *, int, Exif *, int, int, int);
extern void tag_distance(Img *, int, Exif *, int, int, int);
extern void tag_lens(Img *, int, Exif *, int, int, int);

extern int jpgopen(char *, int);
extern long jpgpread(int, void *, long, vlong);
extern int jpgclose(int);
