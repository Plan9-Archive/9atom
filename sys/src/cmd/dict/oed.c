#include <u.h>
#include <libc.h>
#include <bio.h>
#include "dict.h"

enum {
	Buflen=1000,
	Maxaux=5,
};

/* Possible tags */
enum {
	A,		/* author in quote (small caps) */
	B,		/* bold */
	Ba,		/* author inside bib */
	Bch,		/* builtup chem component */
	Bib,		/* surrounds word 'in' for bibliographic ref */
	Bl,		/* bold */
	Bo,		/* bond over */
	Bu,		/* bond under */
	Cb,		/* ? block of stuff (indent) */
	Cf,		/* cross ref to another entry (italics) */
	Chem,		/* chemistry formula */
	Co,		/* over (preceding sum, integral, etc.) */
	Col,		/* column of table (aux just may be r) */
	Cu,		/* under (preceding sum, integral, etc.) */
	Dat,		/* date */
	Db,		/* def block? indent */
	Dn,		/* denominator of fraction */
	E,		/* main entry */
	Ed,		/* editor's comments (in [...]) */
	Etym,		/* etymology (in [...]) */
	Fq,		/* frequency count (superscript) */
	Form,		/* formula */
	Fr,		/* fraction (contains <nu>, then <dn>) */
	Gk,		/* greek (transliteration) */
	Gr,		/* grammar? (e.g., around 'pa.' in 'pa. pple.') */
	Hg,		/* headword group */
	Hm,		/* homonym (superscript) */
	Hw,		/* headword (bold) */
	I,		/* italics */
	Il,		/* italic list? */
	In,		/* inferior (subscript) */
	L,		/* row of col of table */
	La,		/* status or usage label (italic) */
	Lc,		/* chapter/verse sort of thing for works */
	N,		/* note (smaller type) */
	Nu,		/* numerator of fraction */
	Ov,		/* needs overline */
	P,		/* paragraph (indent) */
	Ph,		/* pronunciation (transliteration) */
	Pi,		/* pile (frac without line) */
	Pqp,		/* subblock of quote */
	Pr,		/* pronunciation (in (...)) */
	Ps,		/* position (e.g., adv.) (italic) */
	Pt,		/* part (in lc) */
	Q,		/* quote in quote block */
	Qd,		/* quote date (bold) */
	Qig,		/* quote number (greek) */
	Qla,		/* status or usage label in quote (italic) */
	Qp,		/* quote block (small type, indent) */
	Qsn,		/* quote number */
	Qt,		/* quote words */
	R,		/* roman type style */
	Rx,		/* relative cross reference (e.g., next) */
	S,		/* another form? (italic) */
	S0,		/* sense (sometimes surrounds several sx's) */
	S1,		/* sense (aux num: indented bold letter) */
	S2,		/* sense (aux num: indented bold capital rom num) */
	S3,		/* sense (aux num: indented number of asterisks) */
	S4,		/* sense (aux num: indented bold number) */
	S5,		/* sense (aux num: indented number of asterisks) */
	S6,		/* subsense (aux num: bold letter) */
	S7a,		/* subsense (aux num: letter) */
	S7n,		/* subsense (aux num: roman numeral) */
	Sc,		/* small caps */
	Sgk,		/* subsense (aux num: transliterated greek) */
	Sn,		/* sense of subdefinition (aux num: roman letter) */
	Ss,		/* sans serif */
	Ssb,		/* sans serif bold */
	Ssi,		/* sans serif italic */
	Su,		/* superior (superscript) */
	Sub,		/* subdefinition */
	Table,		/* table (aux cols=number of columns) */
	Tt,		/* title? (italics) */
	Vd,		/* numeric label for variant form */
	Ve,		/* variant entry */
	Vf,		/* variant form (light bold) */
	Vfl,		/* list of vf's (starts with Also or Forms) */
	W,		/* work (e.g., Beowulf) (italics) */
	X,		/* cross reference to main word (small caps) */
	Xd,		/* cross reference to quotation by date */
	Xi,		/* internal cross reference ? (italic) */
	Xid,		/* cross reference identifer, in quote ? */
	Xs,		/* cross reference sense (lower number) */
	Xr,		/* list of x's */
	Ntag		/* end of tags */
};

/* Assoc tables must be sorted on first field */

static Assoc tagtab[] = {
	{"a",		A},
	{"b",		B},
	{"ba",		Ba},
	{"bch",		Bch},
	{"bib",		Bib},
	{"bl",		Bl},
	{"bo",		Bo},
	{"bu",		Bu},
	{"cb",		Cb},
	{"cf",		Cf},
	{"chem",	Chem},
	{"co",		Co},
	{"col",		Col},
	{"cu",		Cu},
	{"dat",		Dat},
	{"db",		Db},
	{"dn",		Dn},
	{"e",		E},
	{"ed",		Ed},
	{"et",		Etym},
	{"etym",	Etym},
	{"form",	Form},
	{"fq",		Fq},
	{"fr",		Fr},
	{"frac",	Fr},
	{"gk",		Gk},
	{"gr",		Gr},
	{"hg",		Hg},
	{"hm",		Hm},
	{"hw",		Hw},
	{"i",		I},
	{"il",		Il},
	{"in",		In},
	{"l",		L},
	{"la",		La},
	{"lc",		Lc},
	{"n",		N},
	{"nu",		Nu},
	{"ov",		Ov},
	{"p",		P},
	{"ph",		Ph},
	{"pi",		Pi},
	{"pqp",		Pqp},
	{"pr",		Pr},
	{"ps",		Ps},
	{"pt",		Pt},
	{"q",		Q},
	{"qd",		Qd},
	{"qig",		Qig},
	{"qla",		Qla},
	{"qp",		Qp},
	{"qsn",		Qsn},
	{"qt",		Qt},
	{"r",		R},
	{"rx",		Rx},
	{"s",		S},
	{"s0",		S0},
	{"s1",		S1},
	{"s2",		S2},
	{"s3",		S3},
	{"s4",		S4},
	{"s5",		S5},
	{"s6",		S6},
	{"s7a",		S7a},
	{"s7n",		S7n},
	{"sc",		Sc},
	{"sgk",		Sgk},
	{"sn",		Sn},
	{"ss",		Ss,},
	{"ssb",		Ssb},
	{"ssi",		Ssi},
	{"su",		Su},
	{"sub",		Sub},
	{"table",	Table},
	{"tt",		Tt},
	{"vd",		Vd},
	{"ve",		Ve},
	{"vf",		Vf},
	{"vfl",		Vfl},
	{"w",		W},
	{"x",		X},
	{"xd",		Xd},
	{"xi",		Xi},
	{"xid",		Xid},
	{"xr",		Xr},
	{"xs",		Xs},
};

/* Possible tag auxilliary info */
enum {
	Cols,		/* number of columns in a table */
	Num,		/* letter or number, for a sense */
	St,		/* status (e.g., obs) */
	Naux
};

static Assoc auxtab[] = {
	{"cols",	Cols},
	{"num",		Num},
	{"st",		St}
};

static Assoc spectab[] = {
	{"3on4",	L'¾'},
	{"3on8", L'⅜'},
	{"Aacu",	L'Á'},
	{"Aang",	L'Å'},
	{"Abarab",	L'Ā'},
	{"Acirc",	L'Â'},
	{"Ae",		L'Æ'},
	{"Agrave",	L'À'},
	{"Alpha",	L'Α'},
	{"Amac",	L'Ā'},
	{"Asg",		L'Ʒ'},		/* Unicyle. Cf "Sake" */
	{"Auml",	L'Ä'},
	{"Beta",	L'Β'},
	{"Cced",	L'Ç'},
	{"Chacek",	L'Č'},
	{"Chi",		L'Χ'},
	{"Chirho",	L'☧'},		/* Chi Rho U+2627 */
	{"Csigma",	L'Ϛ'},
	{"Delta",	L'Δ'},
	{"Eacu",	L'É'},
	{"Ecirc",	L'Ê'},
	{"Edh",		L'Ð'},
	{"Epsilon",	L'Ε'},
	{"Eta",		L'Η'},
	{"Gamma",	L'Γ'},
	{"Iacu",	L'Í'},
	{"Icirc",	L'Î'},
	{"Imac",	L'Ī'},
	{"Integ",	L'∫'},
	{"Iota",	L'Ι'},
	{"Kappa",	L'Κ'},
	{"Koppa",	L'Ϟ'},
	{"Lambda",	L'Λ'},
	{"Lbar",	L'Ł'},
	{"Mu",		L'Μ'},
	{"Naira",	L'₦'},
	{"Nplus",	L'N'},		/* should have plus above */
	{"Ntilde",	L'Ñ'},
	{"Nu",		L'Ν'},
	{"Oacu",	L'Ó'},
	{"Obar",	L'Ø'},
	{"Ocirc",	L'Ô'},
	{"Oe",		L'Œ'},
	{"Omega",	L'Ω'},
	{"Omicron",	L'Ο'},
	{"Ouml",	L'Ö'},
	{"Phi",		L'Φ'},
	{"Pi",		L'Π'},
	{"Psi",		L'Ψ'},
	{"Rho",		L'Ρ'},
	{"Sacu",	L'Ś'},
	{"Sigma",	L'Σ'},
	{"Summ",	L'∑'},
	{"Tau",		L'Τ'},
	{"Th",		L'Þ'},
	{"Theta",	L'Θ'},
	{"Tse",		L'Ц'},
	{"Uacu",	L'Ú'},
	{"Ucirc",	L'Û'},
	{"Upsilon",	L'Υ'},
	{"Uuml",	L'Ü'},
	{"Wyn",		L'ƿ'},		/* wynn U+01BF */
	{"Xi",		L'Ξ'},
	{"Ygh",		L'Ʒ'},		/* Yogh	U+01B7 */
	{"Zeta",	L'Ζ'},
	{"Zh",		L'Ʒ'},		/* looks like Yogh. Cf "Sake" */
	{"a",		L'a'},		/* ante */
	{"aacu",	L'á'},
	{"aang",	L'å'},
	{"aasper",	MAAS},
	{"abreve",	L'ă'},
	{"acirc",	L'â'},
	{"acu",		LACU},
	{"ae",		L'æ'},
	{"agrave",	L'à'},
	{"ahook",	L'ą'},
	{"alenis",	MALN},
	{"alpha",	L'α'},
	{"amac",	L'ā'},
	{"amp",		L'&'},
	{"and",		MAND},
	{"ang",		LRNG},
	{"angle",	L'∠'},
	{"ankh",	L'☥'},		/* ankh U+2625 */
	{"ante",	L'a'},		/* before (year) */
	{"aonq",	MAOQ},
	{"appreq",	L'≃'},
	{"aquar",	L'♒'},
	{"arDadfull",	L'ض'},		/* Dad U+0636 */
	{"arHa",	L'ح'},		/* haa U+062D */
	{"arTa",	L'ت'},		/* taa U+062A */
	{"arain",	L'ع'},		/* ain U+0639 */
	{"arainfull",	L'ع'},		/* ain U+0639 */
	{"aralif",	L'ا'},		/* alef U+0627 */
	{"arba",	L'ب'},		/* baa U+0628 */
	{"arha",	L'ه'},		/* ha U+0647 */
	{"aries",	L'♈'},
	{"arnun",	L'ن'},		/* noon U+0646 */
	{"arnunfull",	L'ن'},		/* noon U+0646 */
	{"arpa",	L'ه'},		/* ha U+0647 */
	{"arqoph",	L'ق'},		/* qaf U+0642 */
	{"arshinfull",	L'ش'},		/* sheen U+0634 */
	{"arta",	L'ت'},		/* taa U+062A */
	{"artafull",	L'ت'},		/* taa U+062A */
	{"artha",	L'ث'},		/* thaa U+062B */
	{"arwaw",	L'و'},		/* waw U+0648 */
	{"arya",	L'ي'},		/* ya U+064A */
	{"aryafull",	L'ي'},		/* ya U+064A */
	{"arzero",	L'٠'},		/* indic zero U+0660 */
	{"asg",		L'ʒ'},		/* unicycle character. Cf "hallow" */
	{"asper",	LASP},
	{"assert",	L'⊢'},
	{"astm",	L'⁂'},		/* asterism: should be upside down */
	{"at",		L'@'},
	{"atilde",	L'ã'},
	{"auml",	L'ä'},
	{"ayin",	L'ع'},		/* arabic ain U+0639 */
	{"b1",		L'-'},		/* single bond */
	{"b2",		L'='},		/* double bond */
	{"b3",		L'≡'},		/* triple bond */
	{"bbar",	L'ƀ'},		/* b with bar U+0180 */
	{"beta",	L'β'},
	{"bigobl",	L'/'},
	{"blC",		L'ℭ'},
	{"blJ",		L'𝔍'},		/* should be black letter */
	{"blU",		L'𝔘'},		/* should be black letter */
	{"blb",		L'𝔟'},		/* should be black letter */
	{"blozenge",	L'◊'},		/* U+25CA; should be black */
	{"bly",		L'𝔶'},		/* should be black letter */
	{"bra",		MBRA},
	{"brbl",	LBRB},
	{"breve",	LBRV},
	{"bslash",	L'\\'},
	{"bsquare",	L'■'},		/* black square U+25A0 */
	{"btril",	L'◀'},		/* U+25C0 */
	{"btrir",	L'▶'},		/* U+25B6 */
	{"c",		L'c'},		/* circa */
	{"cab",		L'〉'},
	{"cacu",	L'ć'},
	{"canc",	L'♋'},
	{"capr",	L'♑'},
	{"caret",	L'^'},
	{"cb",		L'}'},
	{"cbigb",	L'}'},
	{"cbigpren",	L')'},
	{"cbigsb",	L']'},
	{"cced",	L'ç'},
	{"cdil",	LCED},
	{"cdsb",	L'〛'},		/* ]] U+301b */
	{"cent",	L'¢'},
	{"chacek",	L'č'},
	{"chi",		L'χ'},
	{"circ",	LRNG},
	{"circa",	L'c'},		/* about (year) */
	{"circbl",	L'̥'},		/* ring below accent U+0325 */
	{"circle",	L'○'},		/* U+25CB */
	{"circledot",	L'⊙'},
	{"click",	L'ʖ'},
	{"club",	L'♣'},
	{"comtime",	L'C'},
	{"conj",	L'☌'},
	{"cprt",	L'©'},
	{"cq",		L'\''},
	{"cqq",		L'”'},
	{"cross",	L'✠'},		/* maltese cross U+2720 */
	{"crotchet",	L'♩'},
	{"csb",		L']'},
	{"ctilde",	L'c'},		/* +tilde */
	{"ctlig",	MLCT},
	{"cyra",	L'а'},
	{"cyre",	L'е'},
	{"cyrhard",	L'ъ'},
	{"cyrjat",	L'ѣ'},
	{"cyrm",	L'м'},
	{"cyrn",	L'н'},
	{"cyrr",	L'р'},
	{"cyrsoft",	L'ь'},
	{"cyrt",	L'т'},
	{"cyry",	L'ы'},
	{"dag",		L'†'},
	{"dbar",	L'đ'},
	{"dblar",	L'⇋'},
	{"dblgt",	L'≫'},
	{"dbllt",	L'≪'},
	{"dced",	L'ḑ'},
	{"dd",		MDD},
	{"ddag",	L'‡'},
	{"ddd",		MDDD},
	{"decr",	L'↓'},
	{"deg",		L'°'},
	{"dele",	L'd'},		/* should be dele */
	{"delta",	L'δ'},
	{"descnode",	L'☋'},		/* descending node U+260B */
	{"devph",	L'फ'},
	{"diamond",	L'♢'},
	{"digamma",	L'ϝ'},
	{"div",		L'÷'},
	{"dlessi",	L'ı'},
	{"dlessj1",	L'ȷ'},
	{"dlessj2",	L'ȷ'},
	{"dlessj3",	L'ȷ'},
	{"dollar",	L'$'},
	{"dotab",	LDOT},
	{"dotbl",	LDTB},
	{"drachm",	L'ʒ'},
	{"dubh",	L'-'},
	{"eacu",	L'é'},
	{"earth",	L'♁'},
	{"easper",	MEAS},
	{"ebreve",	L'ĕ'},
	{"ecirc",	L'ê'},
	{"edh",		L'ð'},
	{"egrave",	L'è'},
	{"ehacek",	L'ě'},
	{"ehook",	L'ę'},
	{"elem",	L'∊'},
	{"elenis",	MELN},
	{"em",		L'—'},
	{"emac",	L'ē'},
	{"emem",	MEMM},
	{"en",		L'–'},
	{"epsilon",	L'ε'},
	{"equil",	L'⇋'},
	{"ergo",	L'∴'},
	{"es",		MES},
	{"eszett",	L'ß'},
	{"eta",		L'η'},
	{"eth",		L'ð'},
	{"euml",	L'ë'},
	{"expon",	L'↑'},
	{"fact",	L'!'},
	{"fata",	L'ɑ'},
	{"fatpara",	L'¶'},		/* should have fatter, filled in bowl */
	{"female",	L'♀'},
	{"ffilig",	MLFFI},
	{"fflig",	MLFF},
	{"ffllig",	MLFFL},
	{"filig",	MLFI},
	{"flat",	L'♭'},
	{"fllig",	MLFL},
	{"frE",		L'E'},		/* should be curly */
	{"frL",		L'L'},		/* should be curly */
	{"frR",		L'R'},		/* should be curly */
	{"frakB",	L'𝔅'},		/* fraktur style */
	{"frakG",	L'𝔊'},
	{"frakH",	L'𝕳'},		/* should be normal weight */
	{"frakI",	L'𝕴'},			/* should be normal weight */
	{"frakM",	L'𝔐'},
	{"frakU",	L'𝔘'},
	{"frakX",	L'𝔛'},
	{"frakY",	L'𝔜'},
	{"frakh",	L'𝔥'},
	{"frbl",	LFRB},
	{"frown",	LFRN},
	{"fs",		L' '},
	{"fsigma",	L'ς'},
	{"gAacu",	L'Ά'},		/* "tonos" */
	{"gaacu",	L'ά'},		/* "tonos" */
	{"gabreve",	L'α'},		/* +breve u+1fb8 (vrachy) */
	{"gafrown",	L'α'},		/* +frown u+1fb6 */
	{"gagrave",	L'α'},		/* +grave u+1f70 */
	{"gamac",	L'α'},		/* +macron u+1fb1 */
	{"gamma",	L'γ'},
	{"gauml",	L'α'},		/* +umlaut */
	{"ge",		L'≧'},
	{"geacu",	L'έ'},		/* "tonos" */
	{"gegrave",	L'ε'},		/* +grave u+1f72 (varia) */
	{"ghacu",	L'ή'},		/* "tonos" */
	{"ghfrown",	L'η'},		/* +frown u+1fc6 (perispomeni) */
	{"ghgrave",	L'η'},		/* +grave u+1f74 (varia) */
	{"ghmac",	L'η'},		/* +macron */
	{"giacu",	L'ι'},		/* +acute */
	{"gibreve",	L'ί'},		/* "tonos" */
	{"gifrown",	L'ι'},		/* +frown u+1fd6 (perispomeni) */
	{"gigrave",	L'ι'},		/* +grave u+1f76 (varia) */
	{"gimac",	L'ι'},		/* +macron */
	{"giuml",	L'ι'},		/* +umlaut */
	{"glagjat",	L'ѧ'},
	{"glots",	L'ˀ'},
	{"goacu",	L'ό'},		/* "tonos" */
	{"gobreve",	L'ο'},		/* +breve */
	{"grave",	LGRV},
	{"gt",		L'>'},
	{"guacu",	L'ύ'},		/* "tonos" */
	{"gufrown",	L'υ'},		/* +frown u+1fe6 (perispomeni) */
	{"gugrave",	L'υ'},		/* +grave u+1f7a (varia) */
	{"gumac",	L'υ'},		/* +macron */
	{"guuml",	L'υ'},		/* +umlaut */
	{"gwacu",	L'ώ'},		/* "tonos" */
	{"gwfrown",	L'ω'},		/* +frown u+1ff6 (perispomeni) */
	{"gwgrave",	L'ω'},		/* +grave u+1f7c (varia) */
	{"hacek",	LHCK},
	{"halft",	L'⌈'},
	{"hash",	L'#'},
	{"hasper",	MHAS},
	{"hatpath",	L'ֲ'},		/* hataf patah U+05B2 */
	{"hatqam",	L'ֳ'},		/* hataf qamats U+05B3 */
	{"hatseg",	L'ֱ'},		/* hataf segol U+05B1 */
	{"hbar",	L'ħ'},
	{"heart",	L'♡'},
	{"hebaleph",	L'א'},		/* aleph U+05D0 */
	{"hebayin",	L'ע'},		/* ayin U+05E2 */
	{"hebbet",	L'ב'},		/* bet U+05D1 */
	{"hebbeth",	L'ב'},		/* bet U+05D1 */
	{"hebcheth",	L'ח'},		/* bet U+05D7 */
	{"hebdaleth",	L'ד'},		/* dalet U+05D3 */
	{"hebgimel",	L'ג'},		/* gimel U+05D2 */
	{"hebhe",	L'ה'},		/* he U+05D4 */
	{"hebkaph",	L'כ'},		/* kaf U+05DB */
	{"heblamed",	L'ל'},		/* lamed U+05DC */
	{"hebmem",	L'מ'},		/* mem U+05DE */
	{"hebnun",	L'נ'},		/* nun U+05E0 */
	{"hebnunfin",	L'ן'},		/* final nun U+05DF */
	{"hebpe",	L'פ'},		/* pe U+05E4 */
	{"hebpedag",	L'ף'},		/* final pe? U+05E3 */
	{"hebqoph",	L'ק'},		/* qof U+05E7 */
	{"hebresh",	L'ר'},		/* resh U+05E8 */
	{"hebshin",	L'ש'},		/* shin U+05E9 */
	{"hebtav",	L'ת'},		/* tav U+05EA */
	{"hebtsade",	L'צ'},		/* tsadi U+05E6 */
	{"hebwaw",	L'ו'},		/* vav? U+05D5 */
	{"hebyod",	L'י'},		/* yod U+05D9 */
	{"hebzayin",	L'ז'},		/* zayin U+05D6 */
	{"hgz",		L'ʒ'},		/* ??? Cf "alet" */
	{"hireq",	L'ִ'},		/* U+05B4 */
	{"hlenis",	MHLN},
	{"hook",	LOGO},
	{"horizE",	L'E'},		/* should be on side */
	{"horizP",	L'P'},		/* should be on side */
	{"horizS",	L'∽'},
	{"horizT",	L'⊣'},
	{"horizb",	L'{'},		/* should be underbrace */
	{"ia",		L'α'},
	{"iacu",	L'í'},
	{"iasper",	MIAS},
	{"ib",		L'β'},
	{"ibar",	L'ɨ'},
	{"ibreve",	L'ĭ'},
	{"icirc",	L'î'},
	{"id",		L'δ'},
	{"ident",	L'≡'},
	{"ie",		L'ε'},
	{"ifilig",	MLFI},
	{"ifflig",	MLFF},
	{"ig",		L'γ'},
	{"igrave",	L'ì'},
	{"ih",		L'η'},
	{"ii",		L'ι'},
	{"ik",		L'κ'},
	{"ilenis",	MILN},
	{"imac",	L'ī'},
	{"implies",	L'⇒'},
	{"index",	L'☞'},
	{"infin",	L'∞'},
	{"integ",	L'∫'},
	{"intsec",	L'∩'},
	{"invpri",	L'ˏ'},
	{"iota",	L'ι'},
	{"iq",		L'ψ'},
	{"istlig",	MLST},
	{"isub",	L'ϵ'},		/* iota below accent */
	{"iuml",	L'ï'},
	{"iz",		L'ζ'},
	{"jup",		L'♃'},
	{"kappa",	L'κ'},
	{"koppa",	L'ϟ'},
	{"lambda",	L'λ'},
	{"lar",		L'←'},
	{"lbar",	L'ł'},
	{"le",		L'≦'},
	{"lenis",	LLEN},
	{"leo",		L'♌'},
	{"lhalfbr",	L'⌈'},
	{"lhshoe",	L'⊃'},
	{"libra",	L'♎'},
	{"llswing",	MLLS},
	{"lm",		L'ː'},
	{"logicand",	L'∧'},
	{"logicor",	L'∨'},
	{"longs",	L'ʃ'},
	{"lrar",	L'↔'},
	{"lt",		L'<'},
	{"ltappr",	L'≾'},
	{"ltflat",	L'∠'},
	{"lumlbl",	L'l'},		/* +umlaut below */
	{"mac",		LMAC},
	{"male",	L'♂'},
	{"mc",		L'c'},		/* should be raised */
	{"merc",	L'☿'},		/* mercury U+263F */
	{"min",		L'−'},
	{"moonfq",	L'☽'},		/* first quarter moon U+263D */
	{"moonlq",	L'☾'},		/* last quarter moon U+263E */
	{"msylab",	L'm'},		/* +sylab (ˌ) */
	{"mu",		L'μ'},
	{"nacu",	L'ń'},
	{"natural",	L'♮'},
	{"neq",		L'≠'},
	{"nfacu",	L'′'},
	{"nfasper",	L'ʽ'},
	{"nfbreve",	L'˘'},
	{"nfced",	L'¸'},
	{"nfcirc",	L'ˆ'},
	{"nffrown",	L'⌢'},
	{"nfgra",	L'ˋ'},
	{"nfhacek",	L'ˇ'},
	{"nfmac",	L'¯'},
	{"nftilde",	L'˜'},
	{"nfuml",	L'¨'},
	{"ng",		L'ŋ'},
	{"not",		L'¬'},
	{"notelem",	L'∉'},
	{"ntilde",	L'ñ'},
	{"nu",		L'ν'},
	{"oab",		L'〈'},
	{"oacu",	L'ó'},
	{"oasper",	MOAS},
	{"ob",		L'{'},
	{"obar",	L'ø'},
	{"obigb",	L'{'},		/* should be big */
	{"obigpren",	L'('},
	{"obigsb",	L'['},		/* should be big */
	{"obreve",	L'ŏ'},
	{"ocirc",	L'ô'},
	{"odsb",	L'〚'},		/* [[ U+301A */
	{"oe",		L'œ'},
	{"oeamp",	L'&'},
	{"ograve",	L'ò'},
	{"ohook",	L'ǫ'},
	{"olenis",	MOLN},
	{"omac",	L'ō'},
	{"omega",	L'ω'},
	{"omicron",	L'ο'},
	{"ope",		L'ɛ'},
	{"opp",		L'☍'},
	{"oq",		L'`'},
	{"oqq",		L'“'},
	{"or",		MOR},
	{"osb",		L'['},
	{"otilde",	L'õ'},
	{"ouml",	L'ö'},
	{"ounce",	L'℥'},		/* ounce U+2125 */
	{"ovparen",	L'⏜'},
	{"p",		L'′'},
	{"pa",		L'∂'},
	{"page",	L'P'},
	{"pall",	L'ʎ'},
	{"paln",	L'ɲ'},
	{"par",		PAR},
	{"para",	L'¶'},
	{"pbar",	L'p'},		/* +bar */
	{"per",		L'℘'},		/* per U+2118 */
	{"phi",		L'φ'},
	{"phi2",	L'ϕ'},
	{"pi",		L'π'},
	{"pisces",	L'♓'},
	{"planck",	L'ħ'},
	{"plantinJ",	L'𝒥'},		/* should be script */
	{"pm",		L'±'},
	{"pmil",	L'‰'},
	{"pp",		L'″'},
	{"ppp",		L'‴'},
	{"prop",	L'∝'},
	{"psi",		L'ψ'},
	{"pstlg",	L'£'},
	{"q",		L'?'},		/* should be raised */
	{"qamets",	L'ֳ'},		/* U+05B3 */
	{"quaver",	L'♪'},
	{"rar",		L'→'},
	{"rasper",	MRAS},
	{"rdot",	L'·'},
	{"recipe",	L'℞'},		/* U+211E */
	{"reg",		L'®'},
	{"revC",	L'Ɔ'},		/* open O U+0186 */
	{"reva",	L'ɒ'},
	{"revc",	L'ɔ'},
	{"revope",	L'ɜ'},
	{"revr",	L'ɹ'},
	{"revsc",	L'˒'},		/* upside-down semicolon */
	{"revv",	L'ʌ'},
	{"rfa",		L'ǫ'},
	{"rhacek",	L'ř'},
	{"rhalfbr",	L'⌉'},
	{"rho",		L'ρ'},
	{"rhshoe",	L'⊂'},
	{"rlenis",	MRLN},
	{"rsylab",	L'r'},		/* +sylab */
	{"runash",	L'ᚫ'},
	{"rvow",	L'˔'},
	{"sacu",	L'ś'},
	{"sagit",	L'♐'},
	{"sampi",	L'ϡ'},
	{"saturn",	L'♄'},
	{"sced",	L'ş'},
	{"schwa",	L'ə'},
	{"scorpio",	L'♏'},
	{"scrA",	L'𝒜'},			/* u+01d49c */	/* should be script */
	{"scrC",	L'𝒞'},			/* u+01d49e */
	{"scrE",	L'ℰ'},			/* u+2030 */
	{"scrF",	L'ℱ'},			/* u+2031 */
	{"scrI",	L'ℐ'},
	{"scrJ",	L'𝒥'},
	{"scrL",	L'ℒ'},
	{"scrO",	L'𝒪'},
	{"scrP",	L'𝒫'},
	{"scrQ",	L'𝒬'},
	{"scrS",	L'𝒯'},
	{"scrT",	L'𝒯'},
	{"scrb",	L'𝒷'},
	{"scrd",	L'𝒹'},			/* u+01d4b9 mathematical script small */
	{"scrh",	L'𝒽'},			/* u+01d4bd */
	{"scrl",	L'ℓ'},			/* u+2113 cf u+01d4c1 */
	{"scruple",	L'℈'},		/* U+2108 */
	{"sdd",		L'ː'},
	{"sect",	L'§'},
	{"segno",	L'𝄋'},
	{"semE",	L'∃'},
	{"sh",		L'ʃ'},
	{"shacek",	L'š'},
	{"sharp",	L'♯'},
	{"sheva",	L'ְ'},		/* U+05B0 */
	{"shti",	L'ɪ'},
	{"shtsyll",	L'∪'},
	{"shtu",	L'ʊ'},
	{"sidetri",	L'⊲'},
	{"sigma",	L'σ'},
	{"since",	L'∵'},
	{"slge",	L'≥'},		/* should have slanted line under (i.e. u+2a7e) */
	{"slle",	L'≤'},		/* should have slanted line under (i.e. u+2a7d) */
	{"sm",		L'ˈ'},
	{"smm",		L'ˌ'},
	{"spade",	L'♠'},
	{"sqrt",	L'√'},
	{"square",	L'□'},		/* U+25A1 */
	{"squaver",	L'𝅘𝅥𝅯'},		/* sixteenth note */
	{"sqbreve",	L'𝅜'},		/* should be square musical symbol breve */
	{"ssChi",	L'Χ'},		/* should be sans serif */
	{"ssIota",	L'Ι'},
	{"ssOmicron",	L'Ο'},
	{"ssPi",	L'Π'},
	{"ssRho",	L'Ρ'},
	{"ssSigma",	L'Σ'},
	{"ssTau",	L'Τ'},
	{"star",	L'*'},
	{"stlig",	MLST},
	{"sup2",	L'²'},
	{"supgt",	L'˃'},
	{"suplt",	L'˂'},
	{"sur",		L'ʳ'},
	{"swing",	L'∼'},
	{"tau",		L'τ'},
	{"taur",	L'♉'},
	{"th",		L'þ'},
	{"thbar",	L'þ'},		/* +bar */
	{"theta",	L'θ'},
	{"thinqm",	L'?'},		/* should be thinner */
	{"tilde",	LTIL},
	{"times",	L'×'},
	{"tri",		L'∆'},
	{"trli",	L'‖'},
	{"ts",		L' '},
	{"uacu",	L'ú'},
	{"uasper",	MUAS},
	{"ubar",	L'u'},		/* +bar */
	{"ubreve",	L'ŭ'},
	{"ucirc",	L'û'},
	{"udA",		L'∀'},
	{"udT",		L'⊥'},
	{"uda",		L'ɐ'},
	{"udh",		L'ɥ'},
	{"udqm",	L'¿'},
	{"udpsi",	L'⋔'},
	{"udtr",	L'∇'},
	{"ugrave",	L'ù'},
	{"ulenis",	MULN},
	{"umac",	L'ū'},
	{"uml",		LUML},
	{"undl",	L'ˍ'},		/* underline accent */
	{"union",	L'∪'},
	{"upsilon",	L'υ'},
	{"uuml",	L'ü'},
	{"vavpath",	L'ו'},		/* vav U+05D5 (+patah) */
	{"vavsheva",	L'ו'},		/* vav U+05D5 (+sheva) */
	{"vb",		L'|'},
	{"vddd",	L'⋮'},
	{"versicle2",	L'℣'},		/* U+2123 */
	{"vinc",	L'¯'},
	{"virgo",	L'♍'},
	{"vpal",	L'ɟ'},
	{"vvf",		L'ɣ'},
	{"wasper",	MWAS},
	{"wavyeq",	L'≈'},
	{"wlenis",	MWLN},
	{"wyn",		L'ƿ'},		/* wynn U+01BF */
	{"xi",		L'ξ'},
	{"yacu",	L'ý'},
	{"ycirc",	L'ŷ'},
	{"ygh",		L'ʒ'},
	{"ymac",	L'ȳ'},		/* +macron */
	{"yuml",	L'ÿ'},
	{"zced",	L'z'},		/* +cedilla */
	{"zeta",	L'ζ'},
	{"zh",		L'ʒ'},
	{"zhacek",	L'ž'},
};
/*
   The following special characters don't have close enough
   equivalents in Unicode, so aren't in the above table.
	22n		2^(2^n) Cf Fermat
	2on4		2/4
	Bantuo		Bantu O. Cf Otshi-herero
	Car		C with circular arrow on top
	albrtime 	cut-time: C with vertical line		01d135	𝄵
	ardal		Cf dental
	bantuo		Bantu o. Cf Otshi-herero
	bbc1		single chem bond below
	bbc2		double chem bond below
	bbl1		chem bond like /
	bbl2		chem bond like //
	bbr1		chem bond like \
	bbr2		chem bond \\
	bcop1		copper symbol. Cf copper
	bcop2		copper symbol. Cf copper
	benchm		Cf benchmark
	btc1		single chem bond above
	btc2		double chem bond above
	btl1		chem bond like \
	btl2		chem bond like \\
	btr1		chem bond like /
	btr2		chem bond line //
	burman		Cf Burman
	devrfls		devanagari letter. Cf cerebral
	duplong[12]	musical note			??  01f3b5	🎵
	egchi		early form of chi
	eggamma[12]	early form of gamma
	egiota		early form of iota
	egkappa		early form of kappa
	eglambda	early form of lambda
	egmu[12]	early form of mu
	egnu[12]	early form of nu
	egpi[123]	early form of pi
	egrho[12]	early form of rho
	egsampi		early form of sampi
	egsan		early form of san
	egsigma[12]	early form of sigma
	egxi[123]	early form of xi
	elatS		early form of S
	elatc[12]	early form of C
	elatg[12]	early form of G
	glagjeri	Slavonic Glagolitic jeri
	glagjeru	Slavonic Glagolitic jeru
	hypolem		hypolemisk (line with underdot)
	lhrbr		lower half }
	longmord	long mordent
	mbwvow		backwards scretched C. Cf retract.
	mord		music symbol.  Cf mordent
	mostra		Cf direct
	ohgcirc		old form of circumflex
	oldbeta		old form of β. Cf perturbate
	oldsemibr[12]	old forms of semibreve. Cf prolation
	ormg		old form of g. Cf G
	para[12345]	form of ¶
	pauseo		musical pause sign
	pauseu		musical pause sign
	pharyng		Cf pharyngal
	ragr		Black letter ragged r
	repetn		musical repeat. Cf retort		 01d107		𝄇
	segno		musical segno sign		01d10b		𝄋
	semain[12]	semitic ain
	semhe		semitic he
	semheth		semitic heth
	semkaph		semitic kaph
	semlamed[12]	semitic lamed
	semmem		semitic mem
	semnum		semitic nun
	sempe		semitic pe
	semqoph[123]	semitic qoph
	semresh		semitic resh
	semtav[1234]	semitic tav
	semyod		semitic yod
	semzayin[123]	semitic zayin
	shtlong[12]	U with underbar. Cf glyconic
	sigmatau	σ,τ combination
	swast		swastika
	uhrbr		upper half of big }
	versicle1		Cf versicle
 */


static Rune normtab[128] = {
	/*0*/	/*1*/	/*2*/	/*3*/	/*4*/	/*5*/	/*6*/	/*7*/
/*00*/	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
/*10*/	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
/*20*/	L' ',	L'!',	L'"',	L'#',	L'$',	L'%',	SPCS,	L'\'',
	L'(',	L')',	L'*',	L'+',	L',',	L'-',	L'.',	L'/',
/*30*/  L'0',	L'1',	L'2',	L'3',	L'4',	L'5',	L'6',	L'7',
	L'8',	L'9',	L':',	L';',	TAGS,	L'=',	TAGE,	L'?',
/*40*/  L'@',	L'A',	L'B',	L'C',	L'D',	L'E',	L'F',	L'G',
	L'H',	L'I',	L'J',	L'K',	L'L',	L'M',	L'N',	L'O',
/*50*/	L'P',	L'Q',	L'R',	L'S',	L'T',	L'U',	L'V',	L'W',
	L'X',	L'Y',	L'Z',	L'[',	L'\\',	L']',	L'^',	L'_',
/*60*/	L'`',	L'a',	L'b',	L'c',	L'd',	L'e',	L'f',	L'g',
	L'h',	L'i',	L'j',	L'k',	L'l',	L'm',	L'n',	L'o',
/*70*/	L'p',	L'q',	L'r',	L's',	L't',	L'u',	L'v',	L'w',
	L'x',	L'y',	L'z',	L'{',	L'|',	L'}',	L'~',	NONE,
};
static Rune phtab[128] = {
	/*0*/	/*1*/	/*2*/	/*3*/	/*4*/	/*5*/	/*6*/	/*7*/
/*00*/	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
/*10*/	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
/*20*/	L' ',	L'!',	L'ˈ',	L'#',	L'$',	L'ˌ',	L'æ',	L'\'',
	L'(',	L')',	L'*',	L'+',	L',',	L'-',	L'.',	L'/',
/*30*/  L'0',	L'1',	L'2',	L'ɜ',	L'4',	L'5',	L'6',	L'7',
	L'8',	L'ø',	L'ː',	L';',	TAGS,	L'=',	TAGE,	L'?',
/*40*/  L'ə',	L'ɑ',	L'B',	L'C',	L'ð',	L'ɛ',	L'F',	L'G',
	L'H',	L'ɪ',	L'J',	L'K',	L'L',	L'M',	L'ŋ',	L'ɔ',
/*50*/	L'P',	L'ɒ',	L'R',	L'ʃ',	L'θ',	L'ʊ',	L'ʌ',	L'W',
	L'X',	L'Y',	L'ʒ',	L'[',	L'\\',	L']',	L'^',	L'_',
/*60*/	L'`',	L'a',	L'b',	L'c',	L'd',	L'e',	L'f',	L'g',
	L'h',	L'i',	L'j',	L'k',	L'l',	L'm',	L'n',	L'o',
/*70*/	L'p',	L'q',	L'r',	L's',	L't',	L'u',	L'v',	L'w',
	L'x',	L'y',	L'z',	L'{',	L'|',	L'}',	L'~',	NONE,
};
static Rune grtab[128] = {
	/*0*/	/*1*/	/*2*/	/*3*/	/*4*/	/*5*/	/*6*/	/*7*/
/*00*/	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
/*10*/	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
/*20*/	L' ',	L'!',	L'"',	L'#',	L'$',	L'%',	SPCS,	L'\'',
	L'(',	L')',	L'*',	L'+',	L',',	L'-',	L'.',	L'/',
/*30*/  L'0',	L'1',	L'2',	L'3',	L'4',	L'5',	L'6',	L'7',
	L'8',	L'9',	L':',	L';',	TAGS,	L'=',	TAGE,	L'?',
/*40*/  L'@',	L'Α',	L'Β',	L'Ξ',	L'Δ',	L'Ε',	L'Φ',	L'Γ',
	L'Η',	L'Ι',	L'Ϛ',	L'Κ',	L'Λ',	L'Μ',	L'Ν',	L'Ο',
/*50*/	L'Π',	L'Θ',	L'Ρ',	L'Σ',	L'Τ',	L'Υ',	L'V',	L'Ω',
	L'Χ',	L'Ψ',	L'Ζ',	L'[',	L'\\',	L']',	L'^',	L'_',
/*60*/	L'`',	L'α',	L'β',	L'ξ',	L'δ',	L'ε',	L'φ',	L'γ',
	L'η',	L'ι',	L'ς',	L'κ',	L'λ',	L'μ',	L'ν',	L'ο',
/*70*/	L'π',	L'θ',	L'ρ',	L'σ',	L'τ',	L'υ',	L'v',	L'ω',
	L'χ',	L'ψ',	L'ζ',	L'{',	L'|',	L'}',	L'~',	NONE,
};
static Rune subtab[128] = {
	/*0*/	/*1*/	/*2*/	/*3*/	/*4*/	/*5*/	/*6*/	/*7*/
/*00*/	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
/*10*/	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
/*20*/	L' ',	L'!',	L'"',	L'#',	L'$',	L'%',	SPCS,	L'\'',
	L'₍',	L'₎',	L'*',	L'₊',	L',',	L'₋',	L'.',	L'/',
/*30*/  L'₀',	L'₁',	L'₂',	L'₃',	L'₄',	L'₅',	L'₆',	L'₇',
	L'₈',	L'₉',	L':',	L';',	TAGS,	L'₌',	TAGE,	L'?',
/*40*/  L'@',	L'A',	L'B',	L'C',	L'D',	L'E',	L'F',	L'G',
	L'H',	L'I',	L'J',	L'K',	L'L',	L'M',	L'N',	L'O',
/*50*/	L'P',	L'Q',	L'R',	L'S',	L'T',	L'U',	L'V',	L'W',
	L'X',	L'Y',	L'Z',	L'[',	L'\\',	L']',	L'^',	L'_',
/*60*/	L'`',	L'ₐ',	L'b',	L'c',	L'd',	L'ₑ',	L'f',	L'g',
	L'h',	L'i',	L'j',	L'k',	L'l',	L'm',	L'n',	L'ₒ',
/*70*/	L'p',	L'q',	L'r',	L's',	L't',	L'u',	L'v',	L'w',
	L'ₓ',	L'y',	L'z',	L'{',	L'|',	L'}',	L'~',	NONE,
};
static Rune suptab[128] = {
	/*0*/	/*1*/	/*2*/	/*3*/	/*4*/	/*5*/	/*6*/	/*7*/
/*00*/	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
/*10*/	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,	NONE,
/*20*/	L' ',	L'!',	L'"',	L'#',	L'$',	L'%',	SPCS,	L'\'',
	L'⁽',	L'⁾',	L'*',	L'⁺',	L',',	L'⁻',	L'.',	L'/',
/*30*/  L'⁰',	L'¹',	L'²',	L'³',	L'⁴',	L'⁵',	L'⁶',	L'⁷',
	L'⁸',	L'⁹',	L':',	L';',	TAGS,	L'⁼',	TAGE,	L'?',
/*40*/  L'@',	L'A',	L'B',	L'C',	L'D',	L'E',	L'F',	L'G',
	L'H',	L'I',	L'J',	L'K',	L'L',	L'M',	L'N',	L'O',
/*50*/	L'P',	L'Q',	L'R',	L'S',	L'T',	L'U',	L'V',	L'W',
	L'X',	L'Y',	L'Z',	L'[',	L'\\',	L']',	L'^',	L'_',
/*60*/	L'`',	L'a',	L'b',	L'c',	L'd',	L'e',	L'f',	L'g',
	L'h',	L'ⁱ',	L'j',	L'k',	L'l',	L'm',	L'ⁿ',	L'o',
/*70*/	L'p',	L'q',	L'r',	L's',	L't',	L'u',	L'v',	L'w',
	L'x',	L'y',	L'z',	L'{',	L'|',	L'}',	L'~',	NONE,
};

static int	tagstarts;
static char	tag[Buflen];
static int	naux;
static char	auxname[Maxaux][Buflen];
static char	auxval[Maxaux][Buflen];
static char	spec[Buflen];
static char	*auxstate[Naux];	/* vals for most recent tag */
static Entry	curentry;
#define cursize (curentry.end-curentry.start)

static char	*getspec(char *, char *);
static char	*gettag(char *, char *);
static void	dostatus(void);

/*
 * cmd is one of:
 *    'p': normal print
 *    'h': just print headwords
 *    'P': print raw
 */
void
oedprintentry(Entry e, int cmd)
{
	char *p, *pe;
	int t, a, i;
	long r, rprev, rlig;
	Rune *transtab;

	p = e.start;
	pe = e.end;
	transtab = normtab;
	rprev = NONE;
	changett(0, 0, 0);
	curentry = e;
	if(cmd == 'h')
		outinhibit = 1;
	while(p < pe) {
		if(cmd == 'r') {
			outchar(*p++);
			continue;
		}
		r = transtab[(*p++)&0x7F];
		if(r < NONE) {
			/* Emit the rune, but buffer in case of ligature */
			if(rprev != NONE)
				outrune(rprev);
			rprev = r;
		} else if(r == SPCS) {
			/* Start of special character name */
			p = getspec(p, pe);
			r = lookassoc(spectab, asize(spectab), spec);
			if(r == -1) {
				if(debug)
					err("spec %ld %d %s",
						e.doff, cursize, spec);
				r = L'�';
			}
			if(r >= LIGS && r < LIGE) {
				/* handle possible ligature */
				rlig = liglookup(r, rprev);
				if(rlig != NONE)
					rprev = rlig;	/* overwrite rprev */
				else {
					/* could print accent, but let's not */
					if(rprev != NONE) outrune(rprev);
					rprev = NONE;
				}
			} else if(r >= MULTI && r < MULTIE) {
				if(rprev != NONE) {
					outrune(rprev);
					rprev = NONE;
				}
				outrunes(multitab[r-MULTI]);
			} else if(r == PAR) {
				if(rprev != NONE) {
					outrune(rprev);
					rprev = NONE;
				}
				outnl(1);
			} else {
				if(rprev != NONE) outrune(rprev);
				rprev = r;
			}
		} else if(r == TAGS) {
			/* Start of tag name */
			if(rprev != NONE) {
				outrune(rprev);
				rprev = NONE;
			}
			p = gettag(p, pe);
			t = lookassoc(tagtab, asize(tagtab), tag);
			if(t == -1) {
				if(debug)
					err("tag %ld %d %s",
						e.doff, cursize, tag);
				continue;
			}
			for(i = 0; i < Naux; i++)
				auxstate[i] = 0;
			for(i = 0; i < naux; i++) {
				a = lookassoc(auxtab, asize(auxtab), auxname[i]);
				if(a == -1) {
					if(debug)
						err("aux %ld %d %s",
							e.doff, cursize, auxname[i]);
				} else
					auxstate[a] = auxval[i];
			}
			switch(t){
			case E:
			case Ve:
				outnl(0);
				if(tagstarts)
					dostatus();
				break;
			case Ed:
			case Etym:
				outchar(tagstarts? '[' : ']');
				break;
			case Pr:
				outchar(tagstarts? '(' : ')');
				break;
			case In:
				transtab = changett(transtab, subtab, tagstarts);
				break;
			case Hm:
			case Su:
			case Fq:
				transtab = changett(transtab, suptab, tagstarts);
				break;
			case Gk:
				transtab = changett(transtab, grtab, tagstarts);
				break;
			case Ph:
				transtab = changett(transtab, phtab, tagstarts);
				break;
			case Hw:
				if(cmd == 'h') {
					if(!tagstarts)
						outchar(' ');
					outinhibit = !tagstarts;
				}
				break;
			case S0:
			case S1:
			case S2:
			case S3:
			case S4:
			case S5:
			case S6:
			case S7a:
			case S7n:
			case Sn:
			case Sgk:
				if(tagstarts) {
					outnl(2);
					dostatus();
					if(auxstate[Num]) {
						if(t == S3 || t == S5) {
							i = atoi(auxstate[Num]);
							while(i--)
								outchar('*');
							outchars("  ");
						} else if(t == S7a || t == S7n || t == Sn) {
							outchar('(');
							outchars(auxstate[Num]);
							outchars(") ");
						} else if(t == Sgk) {
							i = grtab[auxstate[Num][0]];
							if(i != NONE)
								outrune(i);
							outchars(".  ");
						} else {
							outchars(auxstate[Num]);
							outchars(".  ");
						}
					}
				}
				break;
			case Cb:
			case Db:
			case Qp:
			case P:
				if(tagstarts)
					outnl(1);
				break;
			case Table:
				/*
				 * Todo: gather columns, justify them, etc.
				 * For now, just let colums come out as rows
				 */
				if(!tagstarts)
					outnl(0);
				break;
			case Col:
				if(tagstarts)
					outnl(0);
				break;
			case Dn:
				if(tagstarts)
					outchar('/');
				break;
			}
		}
	}
	if(cmd == 'h') {
		outinhibit = 0;
		outnl(0);
	}
}

/*
 * Return offset into bdict where next oed entry after fromoff starts.
 * Oed entries start with <e>, <ve>, <e st=...>, or <ve st=...>
 */
long
oednextoff(long fromoff)
{
	long a, n;
	int c;

	a = Bseek(bdict, fromoff, 0);
	if(a < 0)
		return -1;
	n = 0;
	for(;;) {
		c = Bgetc(bdict);
		if(c < 0)
			break;
		if(c == '<') {
			c = Bgetc(bdict);
			if(c == 'e') {
				c = Bgetc(bdict);
				if(c == '>' || c == ' ')
					n = 3;
			} else if(c == 'v' && Bgetc(bdict) == 'e') {
				c = Bgetc(bdict);
				if(c == '>' || c == ' ')
					n = 4;
			}
			if(n)
				break;
		}
	}
	return (Boffset(bdict)-n);
}

static char *prkey =
"KEY TO THE PRONUNCIATION\n"
"\n"
"I. CONSONANTS\n"
"b, d, f, k, l, m, n, p, t, v, z: usual English values\n"
"\n"
"g as in go (gəʊ)\n"
"h  ...  ho! (həʊ)\n"
"r  ...  run (rʌn), terrier (ˈtɛriə(r))\n"
"(r)...  her (hɜː(r))\n"
"s  ...  see (siː), success (səkˈsɜs)\n"
"w  ...  wear (wɛə(r))\n"
"hw ...  when (hwɛn)\n"
"j  ...  yes (jɛs)\n"
"θ  ...  thin (θin), bath (bɑːθ)\n"
"ð  ...  then (ðɛn), bathe (beɪð)\n"
"ʃ  ...  shop (ʃɒp), dish (dɪʃ)\n"
"tʃ ...  chop (tʃɒp), ditch (dɪtʃ)\n"
"ʒ  ...  vision (ˈvɪʒən), déjeuner (deʒøne)\n"
"dʒ ...  judge (dʒʌdʒ)\n"
"ŋ  ...  singing (ˈsɪŋɪŋ), think (θiŋk)\n"
"ŋg ...  finger (ˈfiŋgə(r))\n"
"\n"
"Foreign\n"
"ʎ as in It. seraglio (serˈraʎo)\n"
"ɲ  ...  Fr. cognac (kɔɲak)\n"
"x  ...  Ger. ach (ax), Sc. loch (lɒx)\n"
"ç  ...  Ger. ich (ɪç), Sc. nicht (nɪçt)\n"
"ɣ  ...  North Ger. sagen (ˈzaːɣən)\n"
"c  ...  Afrikaans baardmannetjie (ˈbaːrtmanəci)\n"
"ɥ  ...  Fr. cuisine (kɥizin)\n"
"\n"
"II. VOWELS AND DIPTHONGS\n"
"\n"
"Short\n"
"ɪ as in pit (pɪt), -ness (-nɪs)\n"
"ɛ  ...  pet (pɛt), Fr. sept (sɛt)\n"
"æ  ...  pat (pæt)\n"
"ʌ  ...  putt (pʌt)\n"
"ɒ  ...  pot (pɒt)\n"
"ʊ  ...  put (pʊt)\n"
"ə  ...  another (əˈnʌðə(r))\n"
"(ə)...  beaten (ˈbiːt(ə)n)\n"
"i  ...  Fr. si (si)\n"
"e  ...  Fr. bébé (bebe)\n"
"a  ...  Fr. mari (mari)\n"
"ɑ  ...  Fr. bâtiment (bɑtimã)\n"
"ɔ  ...  Fr. homme (ɔm)\n"
"o  ...  Fr. eau (o)\n"
"ø  ...  Fr. peu (pø)\n"
"œ  ...  Fr. boeuf (bœf), coeur (kœr)\n"
"u  ...  Fr. douce (dus)\n"
"ʏ  ...  Ger. Müller (ˈmʏlər)\n"
"y  ...  Fr. du (dy)\n"
"\n"
"Long\n"
"iː as in bean (biːn)\n"
"ɑː ...  barn (bɑːn)\n"
"ɔː ...  born (bɔːn)\n"
"uː ...  boon (buːn)\n"
"ɜː ...  burn (bɜːn)\n"
"eː ...  Ger. Schnee (ʃneː)\n"
"ɛː ...  Ger. Fähre (ˈfɛːrə)\n"
"aː ...  Ger. Tag (taːk)\n"
"oː ...  Ger. Sohn (zoːn)\n"
"øː ...  Ger. Goethe (gøːtə)\n"
"yː ...  Ger. grün (gryːn)\n"
"\n"
"Nasal\n"
"ɛ˜, æ˜ as in Fr. fin (fɛ˜, fæ˜)\n"
"ã  ...  Fr. franc (frã)\n"
"ɔ˜ ...  Fr. bon (bɔ˜n)\n"
"œ˜ ...  Fr. un (œ˜)\n"
"\n"
"Dipthongs, etc.\n"
"eɪ as in bay (beɪ)\n"
"aɪ ...  buy (baɪ)\n"
"ɔɪ ...  boy (bɔɪ)\n"
"əʊ ...  no (nəʊ)\n"
"aʊ ...  now (naʊ)\n"
"ɪə ...  peer (pɪə(r))\n"
"ɛə ...  pair (pɛə(r))\n"
"ʊə ...  tour (tʊə(r))\n"
"ɔə ...  boar (bɔə(r))\n"
"\n"
"III. STRESS\n"
"\n"
"Main stress: ˈ preceding stressed syllable\n"
"Secondary stress: ˌ preceding stressed syllable\n"
"\n"
"E.g.: pronunciation (prəˌnʌnsɪˈeɪʃ(ə)n)\n";
/* TODO: find transcriptions of foreign consonents, œ, ʏ, nasals */

void
oedprintkey(void)
{
	Bprint(bout, "%s", prkey);
}

/*
 * f points just after a '&', fe points at end of entry.
 * Accumulate the special name, starting after the &
 * and continuing until the next '.', in spec[].
 * Return pointer to char after '.'.
 */
static char *
getspec(char *f, char *fe)
{
	char *t;
	int c, i;

	t = spec;
	i = sizeof spec;
	while(--i > 0) {
		c = *f++;
		if(c == '.' || f == fe)
			break;
		*t++ = c;
	}
	*t = 0;
	return f;
}

/*
 * f points just after '<'; fe points at end of entry.
 * Expect next characters from bin to match:
 *  [/][^ >]+( [^>=]+=[^ >]+)*>
 *      tag   auxname auxval
 * Accumulate the tag and its auxilliary information in
 * tag[], auxname[][] and auxval[][].
 * Set tagstarts=1 if the tag is 'starting' (has no '/'), else 0.
 * Set naux to the number of aux pairs found.
 * Return pointer to after final '>'.
 */
static char *
gettag(char *f, char *fe)
{
	char *t;
	int c, i;

	t = tag;
	c = *f++;
	if(c == '/')
		tagstarts = 0;
	else {
		tagstarts = 1;
		*t++ = c;
	}
	i = Buflen;
	naux = 0;
	while(--i > 0) {
		c = *f++;
		if(c == '>' || f == fe)
			break;
		if(c == ' ') {
			*t = 0;
			t = auxname[naux];
			i = Buflen;
			if(naux < Maxaux-1)
				naux++;
		} else if(naux && c == '=') {
			*t = 0;
			t = auxval[naux-1];
			i = Buflen;
		} else
			*t++ = c;
	}
	*t = 0;
	return f;
}

static void
dostatus(void)
{
	char *s;

	s = auxstate[St];
	if(s) {
		if(strcmp(s, "obs") == 0)
			outrune(L'†');
		else if(strcmp(s, "ali") == 0)
			outrune(L'‖');
		else if(strcmp(s, "err") == 0 || strcmp(s, "spu") == 0)
			outrune(L'¶');
		else if(strcmp(s, "xref") == 0)
			{/* nothing */}
		else if(debug)
			err("status %ld %d %s", curentry.doff, cursize, s);
	}
}
