#include <bio.h>
#define MYID "SSH-2.0-Plan9"

#pragma varargck type "M" mpint*

enum {
	CONNSHIFT = 10,
	MAXCONN = 1 << CONNSHIFT,
	LEVSHIFT = 2 * CONNSHIFT + 3,

	RootFile = 0,
	CloneFile = 1 << (2 * CONNSHIFT),
	CtlFile = 2 << (2 * CONNSHIFT),
	DataFile = 3 << (2 * CONNSHIFT),
	ListenFile = 4 << (2 * CONNSHIFT),
	LocalFile = 5 << (2 * CONNSHIFT),
	ReqRemFile = 6 << (2 * CONNSHIFT),
	StatusFile = 7 << (2* CONNSHIFT),
	FileMask = 7 << (2 * CONNSHIFT),
	ConnMask = (1 << CONNSHIFT) - 1,

	Server = 0,
	Client,
};

/*
 * The stylistic anomaly with these names of unbounded length
 * is a result of following the RFCs in using the same names for
 * these constants.  I did that to make it easier to search and
 * cross-reference between the code and the RFCs.
 */
enum		/* SSH2 Protocol Packet Types */
{
	SSH_MSG_DISCONNECT = 1,
	SSH_MSG_IGNORE = 2,
	SSH_MSG_UNIMPLEMENTED,
	SSH_MSG_DEBUG,
	SSH_MSG_SERVICE_REQUEST,
	SSH_MSG_SERVICE_ACCEPT,

	SSH_MSG_KEXINIT = 20,
	SSH_MSG_NEWKEYS,

	SSH_MSG_KEXDH_INIT = 30,
	SSH_MSG_KEXDH_REPLY,

	SSH_MSG_USERAUTH_REQUEST = 50,
	SSH_MSG_USERAUTH_FAILURE,
	SSH_MSG_USERAUTH_SUCCESS,
	SSH_MSG_USERAUTH_BANNER,

	SSH_MSG_USERAUTH_PK_OK = 60,
	SSH_MSG_USERAUTH_PASSWD_CHANGEREQ = 60,

	SSH_MSG_GLOBAL_REQUEST = 80,
	SSH_MSG_REQUEST_SUCCESS,
	SSH_MSG_REQUEST_FAILURE,

	SSH_MSG_CHANNEL_OPEN = 90,
	SSH_MSG_CHANNEL_OPEN_CONFIRMATION,
	SSH_MSG_CHANNEL_OPEN_FAILURE,
	SSH_MSG_CHANNEL_WINDOW_ADJUST,
	SSH_MSG_CHANNEL_DATA,
	SSH_MSG_CHANNEL_EXTENDED_DATA,
	SSH_MSG_CHANNEL_EOF,
	SSH_MSG_CHANNEL_CLOSE,
	SSH_MSG_CHANNEL_REQUEST,
	SSH_MSG_CHANNEL_SUCCESS,
	SSH_MSG_CHANNEL_FAILURE,
};

enum		/* SSH2 reason codes */
{
	SSH_DISCONNECT_HOST_NOT_ALLOWED_TO_CONNECT = 1,
	SSH_DISCONNECT_PROTOCOL_ERROR,
	SSH_DISCONNECT_KEY_EXCHANGE_FAILED,
	SSH_DISCONNECT_RESERVED,
	SSH_DISCONNECT_MAC_ERROR,
	SSH_DISCONNECT_COMPRESSION_ERROR,
	SSH_DISCONNECT_SERVICE_NOT_AVAILABLE,
	SSH_DISCONNECT_PROTOCOL_VERSION_NOT_SUPPORTED,
	SSH_DISCONNECT_HOST_KEY_NOT_VERIFIABLE,
	SSH_DISCONNECT_CONNECTION_LOST,
	SSH_DISCONNECT_BY_APPLICATION,
	SSH_DISCONNECT_TOO_MANY_CONNECTIONS,
	SSH_DISCONNECT_AUTH_CANCELLED_BY_USER,
	SSH_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE,
	SSH_DISCONNECT_ILLEGAL_USR_NAME,

	SSH_OPEN_ADMINISTRATIVELY_PROHIBITED = 1,
	SSH_OPEN_CONNECT_FAILED,
	SSH_OPEN_UNKNOWN_CHANNEL_TYPE,
	SSH_OPEN_RESOURCE_SHORTAGE,
};

enum		/* SSH2 type code */
{
	SSH_EXTENDED_DATA_STDERR = 1,
};

enum		/* connection and channel states */
{
	Empty = 0,
	Allocated,
	Initting,
	Listening,
	Opening,
	Negotiating,
	Authing,
	Established,
	Eof,
	Closing,
	Closed,
};

enum {
	NoKeyFile,
	NoKey,
	KeyWrong,
	KeyOk,
};

typedef struct Plist Plist;
typedef struct SSHChan SSHChan;
typedef struct Conn Conn;
typedef struct Packet Packet;
typedef struct Cipher Cipher;
typedef struct CipherState CipherState;
typedef struct Kex Kex;
typedef struct PKA PKA;
typedef struct MBox MBox;

#pragma incomplete CipherState

struct Plist {
	Packet *pack;
	uchar *st;
	int rem;
	Plist *next;
};

struct SSHChan {
	Rendez r;
	int id, otherid, state;
	int waker;
	int conn;
	ulong rwindow, twindow;
	ulong sent, inrqueue;
	char *ann;
	Req *lreq;
	File *dir, *ctl, *data, *listen, *request, *status;
	Plist *dataq, *datatl, *reqq, *reqtl;
	Channel *inchan, *reqchan;
	QLock xmtlock;
	Rendez xmtrendez;
};

struct Conn {
	QLock l;
	Rendez r;
	Ioproc *dio, *cio, *rio;
	int state;
	int role;
	int id;
	char *remote;
	char *user, *password, *service;
	char *cap;
	char *authkey;
	int nchan;
	int datafd, ctlfd;
	int rpid;
	int inseq, outseq;
	int kexalg, pkalg;
	int cscrypt, ncscrypt, sccrypt, nsccrypt, csmac, ncsmac, scmac, nscmac;
	int encrypt, decrypt, outmac, inmac;
	File *dir, *clonefile, *ctlfile, *datafile, *listenfile, *localfile, *remotefile, *statusfile;
	Packet *skexinit, *rkexinit;
	mpint *x, *e;
	int got_sessid;
	uchar sessid[SHA1dlen];
	uchar c2siv[SHA1dlen*2], nc2siv[SHA1dlen*2], s2civ[SHA1dlen*2], ns2civ[SHA1dlen*2];
	uchar c2sek[SHA1dlen*2], nc2sek[SHA1dlen*2], s2cek[SHA1dlen*2], ns2cek[SHA1dlen*2];
	uchar c2sik[SHA1dlen*2], nc2sik[SHA1dlen*2], s2cik[SHA1dlen*2], ns2cik[SHA1dlen*2];
	char *otherid;
	uchar *inik, *outik;
	CipherState *s2ccs, *c2scs;
	CipherState *enccs, *deccs;
	SSHChan *chans[MAXCONN];
};

struct Packet {
	Conn *c;
	ulong rlength, tlength;
	uchar nlength[4];
	uchar pad_len;
	uchar payload[35000];
};

struct Cipher
{
	char *name;
	int blklen;
	CipherState *(*init)(Conn*, int);
	void (*encrypt)(CipherState*, uchar*, int);
	void (*decrypt)(CipherState*, uchar*, int);
};

struct Kex
{
	char *name;
	int (*serverkex)(Conn *, Packet *);
	int (*clientkex1)(Conn *, Packet *);
	int (*clientkex2)(Conn *, Packet *);
};

struct PKA
{
	char *name;
	Packet *(*ks)(Conn *);
	Packet *(*sign)(Conn *, uchar *, int);
	int (*verify)(Conn *, uchar *, int, char *, char *, int);
};

struct MBox
{
	Channel *mchan;
	char *msg;
	int state;
};

extern int debug;
extern Cipher cipherblowfish, cipher3des, cipherrc4;
extern Cipher cipheraes128, cipheraes192, cipheraes256;
extern Kex dh1sha1, dh14sha1;
extern PKA rsa_pka, dss_pka, *pkas[];
extern sshkeychan[];
extern MBox keymbox;

/* pubkey.c */
RSApub *readpublickey(Biobuf *, char **);
int findkey(char *, char *, RSApub *);
int replacekey(char *, char *, RSApub *);
int appendkey(char *, char *, RSApub *);

/* dh.c */
void dh_init(PKA *[]);

/* transport.c */
Packet 	*new_packet(Conn *);
void		init_packet(Packet *);
void		add_byte(Packet *, char);
void		add_uint32(Packet *, ulong);
ulong	get_uint32(Packet *, uchar **);
int		add_packet(Packet *, void *, int);
void		add_block(Packet *, void *, int);
void		add_string(Packet *, char *);
uchar	*get_string(Packet *, uchar *, char *, int, int *);
void		add_mp(Packet *, mpint *);
mpint	*get_mp(uchar *q);
int		finish_packet(Packet *);
int		undo_packet(Packet *);
void		dump_packet(Packet *);
