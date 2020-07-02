typedef struct Xclue Xclue;
struct Xclue {
	int num;
	int isdown;
	Rune *text;
};

typedef struct Xpt Xpt;
struct Xpt {
	int r;
	int c;
};

typedef struct Xword Xword;
struct Xword {
	int ht;
	int wid;
	int nclue;
	int nloc;

	Rune *title;
	Rune *copyr;
	Rune *author;

	Rune *board;
	Rune *ans;

	Xclue *clue;
	Xpt *loc;
};


Xword*	Brdxword(Biobuf*);
int	Bwrxword(Biobuf*, Xword*);
