#include	"all.h"

char	*errstr9p[MAXERR] =
{
	[Ebadspc]	"attach -- bad specifier",
	[Efid]		"unknown fid",
	[Echar]		"bad character in directory name",
	[Eopen]		"read/write -- on non open fid",
	[Ecount]	"read/write -- count too big",
	[Ealloc]	"phase error -- directory entry not allocated",
	[Eqid]		"phase error -- qid does not match",
	[Eaccess]	"access permission denied",
	[Eentry]	"directory entry not found",
	[Emode]		"open/create -- unknown mode",
	[Edir1]		"walk -- in a non-directory",
	[Edir2]		"create -- in a non-directory",
	[Ephase]	"phase error -- cannot happen",
	[Eexist]	"create/wstat -- file exists",
	[Edot]		"create/wstat -- . and .. illegal names",
	[Eempty]	"remove -- directory not empty",
	[Ebadu]		"attach -- unknown user or failed authentication",
	[Enoattach]	"attach -- system maintenance",
	[Ewstatb]	"wstat -- unknown bits in qid.type/mode",
	[Ewstatd]	"wstat -- attempt to change directory",
	[Ewstatg]	"wstat -- not in group",
	[Ewstatl]	"wstat -- attempt to make length negative",
	[Ewstatm]	"wstat -- attempt to change muid",
	[Ewstato]	"wstat -- not owner or group leader",
	[Ewstatp]	"wstat -- attempt to change qid.path",
	[Ewstatq]	"wstat -- qid.type/dir.mode mismatch",
	[Ewstatu]	"wstat -- not owner",
	[Ewstatv]	"wstat -- attempt to change qid.vers",
	[Ename]		"create/wstat -- bad character in file name",
	[Ewalk]		"walk -- too many (system wide)",
	[Eronly]	"file system read only",
	[Efull]		"file system full",
	[Eoffset]	"read/write -- offset negative",
	[Elocked]	"open/create -- file is locked",
	[Ebroken]	"read/write -- lock is broken",
	[Eauth]		"attach -- authentication failed",
	[Eauth2]	"read/write -- authentication unimplemented",
	[Etoolong]	"name too long",
	[Efidinuse]	"fid in use",
	[Econvert]	"protocol botch",
	[Eversion]	"version conversion",
	[Eauthnone]	"auth -- user 'none' requires no authentication",
	[Eauthdisabled]	"auth -- authentication disabled",	/* development */
	[Eauthfile]	"auth -- out of auth files",
	[Eedge]		"at the bleeding edge",		/* development */
};

char*	tagnames[] =
{
	[Tbuck]		"Tbuck",
	[Tdir]		"Tdir",
	[Tfile]		"Tfile",
	[Tfree]		"Tfree",
	[Tind1]		"Tind1",
	[Tind2]		"Tind2",
#ifndef OLD
	[Tind3]		"Tind3",
	[Tind4]		"Tind4",
	/* add more Tind tags here ... */
#endif
	[Tnone]		"Tnone",
	[Tsuper]	"Tsuper",
	[Tvirgo]	"Tvirgo",
	[Tcache]	"Tcache",
};
