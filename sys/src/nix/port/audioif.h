typedef struct Audio Audio;
typedef struct Volume Volume;

struct Audio
{
	char	*name;

	void	*ctlr;
	void	*mixer;

	Ref	audioopen;

	long	(*read)(Audio *, void *, long, vlong);
	long	(*write)(Audio *, void *, long, vlong);
	void	(*close)(Audio *);

	long	(*volread)(Audio *, void *, long, vlong);
	long	(*volwrite)(Audio *, void *, long, vlong);

	long	(*ctl)(Audio *, void *, long, vlong);
	long	(*status)(Audio *, void *, long, vlong);
	long	(*buffered)(Audio *);

	int	delay;
	int	speed;
	int	bits;
	int	chan;

	int	ctlrno;
	Audio	*next;
};

enum {
	Left,
	Right,
	Stereo,
	Absolute,

	Mono	= Left,
};

struct Volume
{
	char	*name;
	int	reg;
	int	range;
	int	type;
	int	cap;
};

void addaudiocard(char *, int (*)(Audio *));
long genaudiovolread(Audio*, void*, long, vlong, Volume*, int (*)(Audio*, int, int*), ulong);
long genaudiovolwrite(Audio*, void*, long, vlong, Volume*, int (*)(Audio*, int, int *), ulong);
