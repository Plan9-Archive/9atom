#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <auth.h>
#include <ip.h>
#include <pool.h>
#include "sshtun.h"

static int dh_server(Conn *, Packet *, mpint *, int);
static void genkeys(Conn *, uchar [], mpint *);

/*
 * Second Oakley Group from RFC 2409
 */
static char *group1p =
         "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
         "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
         "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
         "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
         "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381"
         "FFFFFFFFFFFFFFFF";

/*
 * 2048-bit MODP group (id 14) from RFC 3526
*/
static char *group14p =
      "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"
      "29024E088A67CC74020BBEA63B139B22514A08798E3404DD"
      "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
      "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
      "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D"
      "C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F"
      "83655D23DCA3AD961C62F356208552BB9ED529077096966D"
      "670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B"
      "E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9"
      "DE2BCBF6955817183995497CEA956AE515D2261898FA0510"
      "15728E5A8AACAA68FFFFFFFFFFFFFFFF";


mpint *two, *p1, *p14;
static DSApriv mydsskey;
static RSApriv myrsakey;

void
dh_init(PKA *pkas[])
{
	char *p, *st, *end;
	int fd, n, k;
	char *buf;

	buf = emalloc9p(4096);
	k = 0;
	pkas[k] = nil;
	st = buf;
	end = buf + 4096;
	fmtinstall('M', mpfmt);
	two = strtomp("2", nil, 10, nil);
	p1 = strtomp(group1p, nil, 16, nil);
	p14 = strtomp(group14p, nil, 16, nil);
	/*
	 * this really should be done through factotum
	 */
	p = getenv("rsakey");
	if (p == nil) {
		fd = open("rsakey", OREAD);
		if (fd < 0) {
			fd = open("/mnt/factotum/ctl", OREAD);
			if (fd < 0)
				goto initdss;
		}
		n = read(fd, buf, 4095);
		buf[n] = 0;
		close(fd);
		st = strstr(buf, "proto=rsa");
		if (st == nil)
			goto initdss;
		end = st;
		for (; st > buf && *st != '\n'; --st) ;
		for (; end < buf+4096 && *end != '\n'; ++end) ;
	}
	else {
		strncpy(buf, p, 4095);
		remove("/env/rsakey");
	}
	p = strstr(st, " n=");
	if (p == nil || p > end) {
		fprint(2, "No key (n) found\n");
		free(buf);
		return;
	}
	myrsakey.pub.n = strtomp(p+3, nil, 16, nil);
	if (debug > 1)
		fprint(2, "n=%M\n", myrsakey.pub.n);
	p = strstr(st, " ek=");
	if (p == nil || p > end) {
		fprint(2, "No key (ek) found\n");
		free(buf);
		return;
	}
	pkas[k++] = &rsa_pka;
	pkas[k] = nil;
	myrsakey.pub.ek = strtomp(p+4, nil, 16, nil);
	if (debug > 1)
		fprint(2, "ek=%M\n", myrsakey.pub.ek);
	p = strstr(st, " !dk=");
	if (p == nil) {
		p = strstr(st, "!dk?");
		if (p == nil || p > end) {
			// fprint(2, "No key (dk) found\n");
			free(buf);
			return;
		}
		else
			goto initdss;
	}
	myrsakey.dk = strtomp(p+5, nil, 16, nil);
	if (debug > 1)
		fprint(2, "dk=%M\n", myrsakey.dk);

initdss:
	p = getenv("dsskey");
	if (p == nil) {
		fd = open("dsskey", OREAD);
		if (fd < 0) {
			fd = open("/mnt/factotum/ctl", OREAD);
			if (fd < 0)
				goto initdss;
		}
		n = read(fd, buf, 4095);
		buf[n] = 0;
		close(fd);
		st = strstr(buf, "proto=dsa");
		if (st == nil) {
			free(buf);
			return;
		}
		end = st;
		for (; st > buf && *st != '\n'; --st) ;
		for (; end < buf+4096 && *end != '\n'; ++end) ;
	}
	else {
		strncpy(buf, p, 4095);
		remove("/env/dsskey");
	}
	p = strstr(buf, " p=");
	if (p == nil || p > end) {
		fprint(2, "No key (p) found\n");
		free(buf);
		return;
	}
	mydsskey.pub.p = strtomp(p+3, nil, 16, nil);
	p = strstr(buf, " q=");
	if (p == nil || p > end) {
		fprint(2, "No key (q) found\n");
		free(buf);
		return;
	}
	mydsskey.pub.q = strtomp(p+3, nil, 16, nil);
	p = strstr(buf, " alpha=");
	if (p == nil || p > end) {
		fprint(2, "No key (g) found\n");
		free(buf);
		return;
	}
	mydsskey.pub.alpha = strtomp(p+7, nil, 16, nil);
	p = strstr(buf, " key=");
	if (p == nil || p > end) {
		fprint(2, "No key (y) found\n");
		free(buf);
		return;
	}
	mydsskey.pub.key = strtomp(p+5, nil, 16, nil);
	pkas[k++] = &dss_pka;
	pkas[k] = nil;
	p = strstr(buf, " !secret=");
	if (p == nil) {
		p = strstr(buf, "!secret?");
		if (p == nil || p > end)
			fprint(2, "No key (x) found\n");
		free(buf);
		return;
	}
	mydsskey.secret = strtomp(p+9, nil, 16, nil);
	free(buf);
}

static Packet *
rsa_ks(Conn *c)
{
	Packet *ks;

	if (myrsakey.pub.ek == nil || myrsakey.pub.n == nil) {
		fprint(2, "No public RSA key info\n");
		return nil;
	}
	ks = new_packet(c);
	add_string(ks, "ssh-rsa");
	add_mp(ks, myrsakey.pub.ek);
	add_mp(ks, myrsakey.pub.n);
	return ks;
}

static void
esma_encode(uchar *h, uchar *em, int nb)
{
	int n, i;
	uchar hh[SHA1dlen];
	static uchar sha1der[] = {0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2b, 0x0e,
		0x03, 0x02, 0x1a, 0x05, 0x00, 0x04, 0x14};

	sha1(h, SHA1dlen, hh, nil);
	n = nb - (15 + SHA1dlen) - 3;
	i = 0;
	em[i++] = 0;
	em[i++] = 1;
	memset(em + i, 0xff, n);
	i += n;
	em[i++] = 0;
	memmove(em + i, sha1der, sizeof(sha1der));
	i += sizeof(sha1der);
	memmove(em + i, hh, SHA1dlen);
}

static Packet *
rsa_sign(Conn *c, uchar *m, int nm)
{
	AuthRpc *ar;
	Packet *sig;
	mpint *s, *mm;
	int fd, n, nbit;
	uchar hh[SHA1dlen];
	uchar *sstr, *em;

	if (myrsakey.dk) {
		nbit = mpsignif (myrsakey.pub.n);
		n = (nbit + 7) / 8;
		sstr = emalloc9p(n);
		em = emalloc9p(n);
		/* Compute s: RFC 3447 */
		esma_encode(m, em, n);
		mm = betomp(em, n, nil);
		s = mpnew(nbit);
		mpexp(mm, myrsakey.dk, myrsakey.pub.n, s);
		mptobe(s, sstr, n, nil);
		mpfree(mm);
		mpfree(s);
		free(em);
	}
	else {
		fd = open("/mnt/factotum/rpc", ORDWR);
		if (fd < 0)
			return nil;
		sha1(m, nm, hh, nil);
		ar = auth_allocrpc(fd);
		if (ar == nil || auth_rpc(ar, "start", "role=sign proto=rsa", 19) != ARok
				|| auth_rpc(ar, "write", hh, SHA1dlen) != ARok
				|| auth_rpc(ar, "read", nil, 0) != ARok) {
			if (debug)
				fprint(2, "got error in factotum: %r\n");
			auth_freerpc(ar);
			close(fd);
			return nil;
		}
		close(fd);
		if (ar->arg == nil)
			return nil;
		sstr = emalloc9p(ar->narg);
		memmove(sstr, ar->arg, ar->narg);
		n = ar->narg;
		auth_freerpc(ar);
	}
	sig = new_packet(c);
	add_string(sig, pkas[c->pkalg]->name);
	add_block(sig, sstr, n);
	free(sstr);
	return sig;
}

/*
 * 0 - If factotum failed, e.g. no key
 * 1 - If key is verified
 * -1 - If factotum found a key, but the verification fails
 */
static int
rsa_verify(Conn *c, uchar *m, int nm, char *user, char *sig, int)
{
	AuthRpc *ar;
//	mpint *s, *mm;
	char *p;
	int fd, n, retval;
//	int nbit;
	uchar hh[SHA1dlen];
//	uchar *sstr, *em;
	char *sigblob;
	char *buf;

	sigblob = emalloc9p(512);
	buf = emalloc9p(256);
	if (debug)
		fprint(2, "In rsa_verify for connection: %d\n", c->id);
#ifdef UNDEF
	if (rsa_exponent) {
		nbit = mpsignif (host_modulus);
		n = (nbit + 7) / 8;
		em = emalloc9p(n);
		/* Compute s: RFC 3447 */
		esma_encode(m, em, n);
		mm = betomp(em, n, nil);
		s = mpnew(1024);
		mpexp(mm, rsa_exponent, host_modulus, s);
		sstr = emalloc9p(n);
		mptobe(s, sstr, n, nil);
		free(em);
		mpfree(mm);
		mpfree(s);
		retval = memcmp(sig, sstr, n);
		free(sstr);
		free(sigblob);
		free(buf);
		if (retval == 0)
			return 1;
		return 0;
	}
	else {
#endif
		retval = 1;
		fd = open("/mnt/factotum/rpc", ORDWR);
		if (fd < 0) {
			if (debug)
				fprint(2, "Could not open factotum RPC: %r\n");
			free(sigblob);
			free(buf);
			return 0;
		}
		p = (char *)get_string(nil, (uchar *)sig, buf, 256, nil);
		get_string(nil, (uchar *)p, sigblob, 512, &n);
		sha1(m, nm, hh, nil);
		if (user != nil)
			p = smprint("role=verify proto=rsa user=%s", user);
		else
			p = smprint("role=verify proto=rsa sys=%s", c->remote);
		ar = auth_allocrpc(fd);
		if (ar == nil || auth_rpc(ar, "start", p, strlen(p)) != ARok
				|| auth_rpc(ar, "write", hh, SHA1dlen) != ARok
				|| auth_rpc(ar, "write", sigblob, n) != ARok
				|| auth_rpc(ar, "read", nil, 0) != ARok) {
			if (debug)
				fprint(2, "got error in factotum: %r\n");
			auth_freerpc(ar);
			free(p);
			close(fd);
			free(sigblob);
			free(buf);
			return 0;
		}
		if (debug)
			fprint(2, "Factotum returned %s\n", ar->ibuf);
		if (strstr(ar->ibuf, "does not verify"))
			retval = -1;
		if (ar != nil)
			auth_freerpc(ar);
		free(p);
		close(fd);
		free(sigblob);
		free(buf);
		return retval;
#ifdef UNDEF
	}
#endif
}

static Packet *
dss_ks(Conn *c)
{
	Packet *ks;

	if (mydsskey.pub.p == nil)
		return nil;
	ks = new_packet(c);
	add_string(ks, "ssh-dss");
	add_mp(ks, mydsskey.pub.p);
	add_mp(ks, mydsskey.pub.q);
	add_mp(ks, mydsskey.pub.alpha);
	add_mp(ks, mydsskey.pub.key);
	return ks;
}

static Packet *
dss_sign(Conn *c, uchar *m, int nm)
{
	AuthRpc *ar;
	DSAsig *s;
	Packet *sig;
	mpint *mm;
	int fd;
	uchar sstr[2*SHA1dlen];

	sha1(m, nm, sstr, nil);
	sig = new_packet(c);
	add_string(sig, pkas[c->pkalg]->name);
	if (mydsskey.secret) {
		mm = betomp(sstr, SHA1dlen, nil);
		s = dsasign(&mydsskey, mm);
		mptobe(s->r, sstr, SHA1dlen, nil);
		mptobe(s->s, sstr+SHA1dlen, SHA1dlen, nil);
		dsasigfree(s);
		mpfree(mm);
	}
	else {
		fd = open("/mnt/factotum/rpc", ORDWR);
		if (fd < 0)
			return nil;
		ar = auth_allocrpc(fd);
		if (ar == nil || auth_rpc(ar, "start", "role=sign proto=dsa", 19) != ARok
				|| auth_rpc(ar, "write", sstr, SHA1dlen) != ARok
				|| auth_rpc(ar, "read", nil, 0) != ARok) {
			if (debug)
				fprint(2, "got error in factotum: %r\n");
			auth_freerpc(ar);
			close(fd);
			return nil;
		}
		close(fd);
		memmove(sstr, ar->arg, ar->narg);
		auth_freerpc(ar);
	}
	add_block(sig, sstr, 2*SHA1dlen);
	return sig;
}

static int
dss_verify(Conn *c, uchar *m, int nm, char *user, char *sig, int nsig)
{
	if (debug)
		fprint(2, "In dss_verify for connection: %d\n", c->id);
	USED(c);
	USED(m);
	USED(nm);
	USED(user);
	USED(sig);
	USED(nsig);

	fprint(2, "DSS verify not supported yet, sorry\n");
	return 0;
}

static int
dh_server1(Conn *c, Packet *pack1)
{
	return dh_server(c, pack1, p1, 1024);
}

static int
dh_server14(Conn *c, Packet *pack1)
{
	return dh_server(c, pack1, p14, 2048);
}

static int
dh_server(Conn *c, Packet *pack1, mpint *grp, int nbit)
{
	Packet *pack2, *ks, *sig;
	mpint *y, *e, *f, *k;
	int n;
	uchar h[SHA1dlen];

	qlock(&c->l);
	f = mpnew(nbit);
	k = mpnew(nbit);
	/* Compute f: RFC4253 */
	y = mprand(nbit / 8, genrandom, nil);
	if (debug > 1)
		fprint(2, "y=%M\n", y);
	mpexp(two, y, grp, f);
	if (debug > 1)
		fprint(2, "f=%M\n", f);
	/* Compute k: RFC4253 */
	if (debug > 1)
		dump_packet(pack1);
	e = get_mp(pack1->payload+1);
	if (debug > 1)
		fprint(2, "e=%M\n", e);
	mpexp(e, y, grp, k);
	if (debug > 1)
		fprint(2, "k=%M\n", k);
	/* Compute H: RFC 4253 */
	pack2 = new_packet(c);
	if (debug)
		fprint(2, "ID strings: %s---%s\n", c->otherid, MYID);
	add_string(pack2, c->otherid);
	add_string(pack2, MYID);
	if (debug > 1) {
		fprint(2, "received kexinit:");
		dump_packet(c->rkexinit);
		fprint(2, "\nsent kexinit:");
		dump_packet(c->skexinit);
	}
	add_block(pack2, c->rkexinit->payload, c->rkexinit->rlength - 1);
	add_block(pack2, c->skexinit->payload, c->skexinit->rlength - c->skexinit->pad_len - 1);
	ks = pkas[c->pkalg]->ks(c);
	if (ks == nil) {
		free(pack2);
		mpfree(y);
		mpfree(e);
		mpfree(f);
		mpfree(k);
		return -1;
	}
	add_block(pack2, ks->payload, ks->rlength - 1);
	add_mp(pack2, e);
	add_mp(pack2, f);
	add_mp(pack2, k);
	sha1(pack2->payload, pack2->rlength - 1, h, nil);
	if (c->got_sessid == 0) {
		memmove(c->sessid, h, SHA1dlen);
		c->got_sessid = 1;
	}
	sig = pkas[c->pkalg]->sign(c, h, SHA1dlen);
	if (sig == nil) {
		fprint(2, "Failed to generate signature\n");
		mpfree(f);
		mpfree(e);
		mpfree(k);
		mpfree(y);
		free(sig);
		free(ks);
		free(pack2);
		qunlock(&c->l);
		return -1;
	}
	/* Send (K_s || f || s) to client: RFC4253 */
	init_packet(pack2);
	pack2->c = c;
	add_byte(pack2, SSH_MSG_KEXDH_REPLY);
	add_block(pack2, ks->payload, ks->rlength - 1);
	add_mp(pack2, f);
	add_block(pack2, sig->payload, sig->rlength - 1);
	if (debug)
		dump_packet(pack2);
	n = finish_packet(pack2);
	if (debug > 1) {
		fprint(2, "Writing %d bytes: len:%d\n", n, nhgetl(pack2->nlength));
		dump_packet(pack2);
	}
	iowrite(c->dio, c->datafd, pack2->nlength, n);

	genkeys(c, h, k);

	/* Send SSH_MSG_NEWKEYS */
	init_packet(pack2);
	pack2->c = c;
	add_byte(pack2, SSH_MSG_NEWKEYS);
	n = finish_packet(pack2);
	iowrite(c->dio, c->datafd, pack2->nlength, n);

	mpfree(f);
	mpfree(e);
	mpfree(k);
	mpfree(y);
	free(sig);
	free(ks);
	free(pack2);
	qunlock(&c->l);
	return 0;
}

static int
dh_client11(Conn *c, Packet *)
{
	Packet *p;
	int n;

	if (c->e)
		mpfree(c->e);
	c->e = mpnew(1024);
	/* Compute e: RFC4253 */
	if (c->x)
		mpfree(c->x);
	c->x = mprand(128, genrandom, nil);
	mpexp(two, c->x, p1, c->e);
	p = new_packet(c);
	add_byte(p, SSH_MSG_KEXDH_INIT);
	add_mp(p, c->e);
	n = finish_packet(p);
	iowrite(c->dio, c->datafd, p->nlength, n);
	free(p);
	return 0;
}

static int
dh_client12(Conn *c, Packet *p)
{
	Packet *ks, *sig, *pack2;
	RSApub *srvkey;
	mpint *f, *k;
	char *newkey, *r, *home;
	uchar *q;
	int n, fd, retval;
	uchar h[SHA1dlen];
	char buf[10];

	ks = new_packet(c);
	sig = new_packet(c);
	pack2 = new_packet(c);
	q = get_string(p, p->payload+1, (char *)ks->payload, 35000, &n);
	ks->rlength = n + 1;
	f = get_mp(q);
	q += nhgetl(q) + 4;
	get_string(p, q, (char *)sig->payload, 35000, &n);
	sig->rlength = n;
	k = mpnew(1024);
	mpexp(f, c->x, p1, k);
	/* Compute H: RFC 4253 */
	init_packet(pack2);
	pack2->c = c;
	if (debug > 1)
		fprint(2, "ID strings: %s---%s\n", c->otherid, MYID);
	add_string(pack2, MYID);
	add_string(pack2, c->otherid);
	if (debug > 1) {
		fprint(2, "received kexinit:");
		dump_packet(c->rkexinit);
		fprint(2, "\nsent kexinit:");
		dump_packet(c->skexinit);
	}
	add_block(pack2, c->skexinit->payload, c->skexinit->rlength - c->skexinit->pad_len - 1);
	add_block(pack2, c->rkexinit->payload, c->rkexinit->rlength - 1);
	add_block(pack2, ks->payload, ks->rlength - 1);
	add_mp(pack2, c->e);
	add_mp(pack2, f);
	add_mp(pack2, k);
	sha1(pack2->payload, pack2->rlength - 1, h, nil);
	mpfree(f);
	if (c->got_sessid == 0) {
		memmove(c->sessid, h, SHA1dlen);
		c->got_sessid = 1;
	}
	if (debug)
		fprint(2, "Verifying server signature\n");
	q = get_string(ks, ks->payload, buf, 10, nil);
	srvkey = emalloc9p(sizeof (RSApub));
	srvkey->ek = get_mp(q);
	q += nhgetl(q) + 4;
	srvkey->n = get_mp(q);
	retval = 0;
	if (findkey("/sys/lib/ssh/keyring", c->remote, srvkey) != KeyOk) {
		home = getenv("home");
		if (home == nil) {
			newkey = "No home directory for key file";
			if (keymbox.msg)
				free(keymbox.msg);
			keymbox.msg = smprint("b%04ld%s", strlen(newkey), newkey);
			nbsendul(keymbox.mchan, 1);
			mpfree(srvkey->ek);
			mpfree(srvkey->n);
			mpfree(k);
			free(ks);
			free(sig);
			free(pack2);
			free(srvkey);
			return -1;
		}
		r = smprint("%s/lib/keyring", home);
		free(home);
		if ((n = findkey(r, c->remote, srvkey)) != KeyOk) {
			newkey = smprint("ek=%M n=%M", srvkey->ek, srvkey->n);
			if (keymbox.msg)
				free(keymbox.msg);
			if (n == NoKeyFile || n == NoKey)
				keymbox.msg = smprint("c%04ld%s", strlen(newkey), newkey);
			else
				keymbox.msg = smprint("b%04ld%s", strlen(newkey), newkey);
			free(newkey);
			nbsendul(keymbox.mchan, 1);
			recvul(keymbox.mchan);
			if (keymbox.msg == nil || keymbox.msg[0] == 'n') {
				free(keymbox.msg);
				keymbox.msg = nil;
				newkey = "Server key reject";
				keymbox.msg = smprint("f%04ld%s", strlen(newkey), newkey);
				nbsendul(keymbox.mchan, 1);
				free(r);
				mpfree(k);
				mpfree(srvkey->ek);
				mpfree(srvkey->n);
				free(ks);
				free(sig);
				free(pack2);
				free(srvkey);
				return -1;
			}
			else {
				if (debug)
					fprint(2, "Adding key\n");
				if (keymbox.msg[0] == 'y')
					appendkey(r, c->remote, srvkey);
				else if (keymbox.msg[0] == 'r')
					replacekey(r, c->remote, srvkey);
			}
		}
		free(r);
	}
	newkey = smprint("key proto=rsa role=verify sys=%s size=%d ek=%M n=%M",
		c->remote, mpsignif (srvkey->n), srvkey->ek, srvkey->n);
	fd = open("/mnt/factotum/ctl", OWRITE);
	if (fd >= 0) {
		write(fd, newkey, strlen(newkey));
		close(fd);
	}
	else
		if (debug)
			fprint(2, "Factotum open failed: %r\n");
	free(newkey);
	mpfree(srvkey->ek);
	mpfree(srvkey->n);
	if (keymbox.msg)
		free(keymbox.msg);
	keymbox.msg = nil;
	n = pkas[c->pkalg]->verify(c, h, SHA1dlen, nil, (char *)sig->payload, sig->rlength);
	newkey = smprint("delkey proto=rsa role=verify sys=%s", c->remote);
	fd = open("/mnt/factotum/ctl", OWRITE);
	if (fd >= 0) {
		write(fd, newkey, strlen(newkey));
		close(fd);
	}
	free(newkey);
	switch (n) {
	case -1:
		newkey = "Signature verifcation failed";
		keymbox.msg = smprint("f%04ld%s", strlen(newkey), newkey);
		retval = -1;
		break;
	case 1:
		keymbox.msg = smprint("o0000");
		break;
	case 0:
		newkey = "Key verification dialog failed";
		keymbox.msg = smprint("f%04ld%s", strlen(newkey), newkey);
		retval = -1;
		break;
	}
	nbsendul(keymbox.mchan, 1);
	if (retval == 0)
		genkeys(c, h, k);
	mpfree(k);
	free(ks);
	free(sig);
	free(pack2);
	free(srvkey);
	return retval;
}

static int
dh_client141(Conn *c, Packet *)
{
	Packet *p;
	mpint *e, *x;
	int n;

	e = mpnew(2048);
	/* Compute e: RFC4253 */
	x = mprand(256, genrandom, nil);
	mpexp(two, x, p14, e);
	p = new_packet(c);
	add_byte(p, SSH_MSG_KEXDH_INIT);
	add_mp(p, e);
	n = finish_packet(p);
	iowrite(c->dio, c->datafd, p->nlength, n);
	free(p);
	mpfree(e);
	mpfree(x);
	return 0;
}

static int
dh_client142(Conn *, Packet *)
{
	return 0;
}

static void
genkeys(Conn *c, uchar h[], mpint *k)
{
	Packet *pack2;
	char buf[82], *bp, *be;
	int n;

	pack2 = new_packet(c);
	/* Compute 40 bytes (320 bits) of keys: each alg can use what it needs */
	/* Client to server IV */
	if (debug > 1) {
		fprint(2, "k=%M\nh=", k);
		for (n = 0; n < SHA1dlen; ++n) fprint(2, "%02ux", h[n]);
		fprint(2, "\nsessid=");
		for (n = 0; n < SHA1dlen; ++n) fprint(2, "%02ux", c->sessid[n]);
		fprint(2, "\n");
	}
	init_packet(pack2);
	add_mp(pack2, k);
	add_packet(pack2, h, SHA1dlen);
	add_byte(pack2, 'A');
	add_packet(pack2, c->sessid, SHA1dlen);
	sha1(pack2->payload, pack2->rlength - 1, c->nc2siv, nil);
	init_packet(pack2);
	add_mp(pack2, k);
	add_packet(pack2, h, SHA1dlen);
	add_packet(pack2, c->nc2siv, SHA1dlen);
	sha1(pack2->payload, pack2->rlength - 1, c->nc2siv + SHA1dlen, nil);
	/* Server to client IV */
	init_packet(pack2);
	add_mp(pack2, k);
	add_packet(pack2, h, SHA1dlen);
	add_byte(pack2, 'B');
	add_packet(pack2, c->sessid, SHA1dlen);
	sha1(pack2->payload, pack2->rlength - 1, c->ns2civ, nil);
	init_packet(pack2);
	add_mp(pack2, k);
	add_packet(pack2, h, SHA1dlen);
	add_packet(pack2, c->ns2civ, SHA1dlen);
	sha1(pack2->payload, pack2->rlength - 1, c->ns2civ + SHA1dlen, nil);
	/* Client to server encryption key */
	init_packet(pack2);
	add_mp(pack2, k);
	add_packet(pack2, h, SHA1dlen);
	add_byte(pack2, 'C');
	add_packet(pack2, c->sessid, SHA1dlen);
	sha1(pack2->payload, pack2->rlength - 1, c->nc2sek, nil);
	init_packet(pack2);
	add_mp(pack2, k);
	add_packet(pack2, h, SHA1dlen);
	add_packet(pack2, c->nc2sek, SHA1dlen);
	sha1(pack2->payload, pack2->rlength - 1, c->nc2sek + SHA1dlen, nil);
	/* Server to client encryption key */
	init_packet(pack2);
	add_mp(pack2, k);
	add_packet(pack2, h, SHA1dlen);
	add_byte(pack2, 'D');
	add_packet(pack2, c->sessid, SHA1dlen);
	sha1(pack2->payload, pack2->rlength - 1, c->ns2cek, nil);
	init_packet(pack2);
	add_mp(pack2, k);
	add_packet(pack2, h, SHA1dlen);
	add_packet(pack2, c->ns2cek, SHA1dlen);
	sha1(pack2->payload, pack2->rlength - 1, c->ns2cek + SHA1dlen, nil);
	/* Client to server integrity key */
	init_packet(pack2);
	add_mp(pack2, k);
	add_packet(pack2, h, SHA1dlen);
	add_byte(pack2, 'E');
	add_packet(pack2, c->sessid, SHA1dlen);
	sha1(pack2->payload, pack2->rlength - 1, c->nc2sik, nil);
	init_packet(pack2);
	add_mp(pack2, k);
	add_packet(pack2, h, SHA1dlen);
	add_packet(pack2, c->nc2sik, SHA1dlen);
	sha1(pack2->payload, pack2->rlength - 1, c->nc2sik + SHA1dlen, nil);
	/* Server to client integrity key */
	init_packet(pack2);
	add_mp(pack2, k);
	add_packet(pack2, h, SHA1dlen);
	add_byte(pack2, 'F');
	add_packet(pack2, c->sessid, SHA1dlen);
	sha1(pack2->payload, pack2->rlength - 1, c->ns2cik, nil);
	init_packet(pack2);
	add_mp(pack2, k);
	add_packet(pack2, h, SHA1dlen);
	add_packet(pack2, c->ns2cik, SHA1dlen);
	sha1(pack2->payload, pack2->rlength - 1, c->ns2cik + SHA1dlen, nil);
	if (debug > 1) {
		be = buf + 82;
		fprint(2, "Client to server IV:\n");
		for (n = 0, bp = buf; n < SHA1dlen*2; ++n) bp = seprint(bp, be, "%02x", c->nc2siv[n]);
		fprint(2, "%s\n", buf);
		fprint(2, "Server to client IV:\n");
		for (n = 0, bp = buf; n < SHA1dlen*2; ++n) bp = seprint(bp, be, "%02x", c->ns2civ[n]);
		fprint(2, "%s\n", buf);
		fprint(2, "Client to server EK:\n");
		for (n = 0, bp = buf; n < SHA1dlen*2; ++n) bp = seprint(bp, be, "%02x", c->nc2sek[n]);
		fprint(2, "%s\n", buf);
		fprint(2, "Server to client EK:\n");
		for (n = 0, bp = buf; n < SHA1dlen*2; ++n) bp = seprint(bp, be, "%02x", c->ns2cek[n]);
		fprint(2, "%s\n", buf);
	}
	free(pack2);
}

Kex dh1sha1 = {
	"diffie-hellman-group1-sha1",
	dh_server1,
	dh_client11,
	dh_client12
};

Kex dh14sha1 = {
	"diffie-hellman-group14-sha1",
	dh_server14,
	dh_client141,
	dh_client142
};

PKA rsa_pka = {
	"ssh-rsa",
	rsa_ks,
	rsa_sign,
	rsa_verify
};

PKA dss_pka = {
	"ssh-dss",
	dss_ks,
	dss_sign,
	dss_verify
};
