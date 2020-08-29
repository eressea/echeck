/*
 * Eressea PBeM Order Syntax Checker Copyright 1997-2012
 * - Enno Rehling (enno@eressea.de)
 * - Christian Schlittchen (corwin@amber.kn-bremen.de)
 * - Katja Zedel (katze@felidae.kn-bremen.de)
 * - Henning Peters (faroul@faroul.de)
 * based on:
 * - Atlantis v1.0 Copyright 1993 by Russell Wallace
 * - Atlantis v1.7 Copyright 1996 by Alex Schröder
 * This program is freeware. It may not be sold or used commercially.
 * Please send any changes or bugfixes to the authors.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <wchar.h>
#include <wctype.h>

#ifdef WITH_CUTEST
#include <CuTest.h>
int AddTestSuites(CuSuite * suite, const char *names);
#endif

#include "config.h"
#include "unicode.h"

static const char *echeck_version = "4.4.9";

#define DEFAULT_PATH "."

enum {
  UT_NONE,
  UT_PARAM,
  UT_ITEM,
  UT_SKILL,
  UT_KEYWORD,
  UT_BUILDING,
  UT_HERB,
  UT_POTION,
  UT_RACE,
  UT_SPELL,
  UT_SHIP,
  UT_OPTION,
  UT_DIRECTION,
  UT_ERRORS,
  UT_HELP,
  UT_MAX
};

static char *Keys[UT_MAX] = {
  "",
  "PARAM",
  "ITEM",
  "SKILL",
  "KEYWORD",
  "BUILDING",
  "HERB",
  "POTION",
  "RACE",
  "SPELL",
  "SHIP",
  "OPTION",
  "DIRECTION",
  "MSG",
  "HELP",
};

/*
 * Diese Dinge müssen geladen sein, damit es überhaupt klappt
 */
#define HAS_PARAM       0x01
#define HAS_ITEM        0x02
#define HAS_SKILL       0x04
#define HAS_KEYWORD     0x08
#define HAS_DIRECTION   0x10
#define HAS_MESSAGES    0x20
#define HAS_ALL         0x3F

typedef struct _ech_file {
  const char *name;
  int type;
} t_ech_file;

const t_ech_file ECheck_Files[] = {
  {"help.txt", UT_HELP},
  {"messages.txt", UT_ERRORS},
  {"parameters.txt", UT_PARAM},
  {"items.txt", UT_ITEM},
  {"skills.txt", UT_SKILL},
  {"commands.txt", UT_KEYWORD},
  {"buildings.txt", UT_BUILDING},
  {"herbs.txt", UT_HERB},
  {"potions.txt", UT_POTION},
  {"races.txt", UT_RACE},
  {"spells.txt", UT_SPELL},
  {"ships.txt", UT_SHIP},
  {"options.txt", UT_OPTION},
  {"directions.txt", UT_DIRECTION},

  /*
   * generische Datei, kann alles enthalten, mit KEYWORD-Erkennung
   */
  {"tokens.txt", -1}
};

const int filecount = sizeof(ECheck_Files) / sizeof(ECheck_Files[0]);
int verbose = 1;
static int compact = 0;

#define SPACE_REPLACEMENT   '~'
#define SPACE               ' '
#define ESCAPE_CHAR         '\\'
#define COMMENT_CHAR        ';'
#define SINGLE_QUOTE        '\''
#define DOUBLE_QUOTE        '"'
#define MARGIN              78
#define RECRUIT_COST        50

#define INDENT_FACTION      0
#define INDENT_UNIT         2
#define INDENT_ORDERS       4
#define INDENT_NEW_ORDERS   6

#define BUFSIZE 8192
#define MAXLINE 4096
#define DESCRIBESIZE 4095
#define NAMESIZE 127

FILE *ERR, *OUT = 0;

const char *run_tests = 0;
int line_no,                    /* count line number */
 filesread = 0;

char echo_it = 0,               /* option: echo input lines */
  no_comment = -3,              /* Keine Infos in [] hinter EINHEIT */
  show_warnings = 4,            /* option: print warnings (levels) */
  warnings_cl = 0,              /* -w auf der Kommandozeile gegeben */
  warn_off = 0,                 /* ECHECK NOWARN */
  use_stderr = 0,               /* option: use stderr for errors etc */
  brief = 0,                    /* option: don't list errors */
  ignore_NameMe = 0,            /* option: ignoriere NameMe-Kommentare ;; */
  piping = 0,                   /* option: wird output als pipe-input  benutzt? */
  lohn = 10,                    /* Lohn für Arbeit - je Region zu setzen */
  silberpool = 1,               /* option: Silberpool-Verwaltung */
  line_start = 0,               /* option: Beginn der Zeilenzählung */
  noship = 0, noroute = 0, nolost = 0, bang_cmd = 0, at_cmd = 0, attack_warning = 0, compile = 0;    /* option: compiler-/magellan-style  warnings */
int error_count = 0,            /* counter: errors */
  warning_count = 0;            /* counter: warnings */
char order_buf[BUFSIZE],        /* current order line */
 checked_buf[BUFSIZE],          /* checked order line */
 message_buf[BUFSIZE],          /* messages are composed here */
 warn_buf[BUFSIZE],             /* warnings are composed here */
 indent, next_indent,           /* indent index */
 does_default = 0,              /* Ist DEFAULT aktiv? */
  befehle_ende,                 /* EOF der Befehlsdatei */
  *echeck_locale = "de", *echeck_rules = "e2", *filename;
int rec_cost = RECRUIT_COST, this_command, this_unit,   /* wird von getaunit gesetzt */
  Rx, Ry;                       /* Koordinaten der aktuellen Region */
static char *path;
FILE *F;

enum {
  OUT_NORMAL,
  OUT_COMPILE,
  OUT_MAGELLAN
};

typedef struct _liste {
  struct _liste *next;
  char *name;
} t_liste;

enum {
  K_ALLIANCE,
  K_PAY,
  K_WORK,
  K_ATTACK,
  K_STEAL,
  K_SIEGE,
  K_NAME,
  K_USE,
  K_DESCRIBE,
  K_ENTER,
  K_GUARD,
  K_MESSAGE,
  K_END,
  K_RIDE,
  K_FOLLOW,
  K_RESEARCH,
  K_GIVE,
  K_HELP,
  K_FIGHT,
  K_COMBATMAGIC,
  K_BUY,
  K_CONTACT,
  K_TEACH,
  K_STUDY,
  K_MAKE,
  K_MOVE,
  K_PASSWORD,
  K_PLANT,
  K_RECRUIT,
  K_REPORT,
  K_OPTION,
  K_SPY,
  K_SETSTEALTH,
  K_CARRY,
  K_QUIT,
  K_TAX,
  K_ENTERTAIN,
  K_SELL,
  K_LEAVE,
  K_CAST,
  K_SHOW,
  K_DESTROY,
  K_FORGET,
  K_DEFAULT,
  K_COMMENT,
  K_ROUTE,
  K_SABOTAGE,
  K_BREED,
  K_ORIGIN,
  K_EMAIL,
  K_RESERVE,
  K_CLAIM,
  K_BANNER,
  K_NUMBER,
  K_SCHOOL,
  K_PIRACY,
  K_RESTART,
  K_GROUP,
  K_SORT,
  K_PREFIX,
  K_PROMOTION,
  K_PROMOTE,
  K_LANGUAGE,
  K_ORDERSTART,
  K_NEXT,
  K_LOCALE,
  K_UNIT,
  K_REGION,
  MAXKEYWORDS
};

static char *Keywords[MAXKEYWORDS] = {
  "ALLIANCE",
  "PAY",
  "WORK",
  "ATTACK",
  "STEAL",
  "SIEGE",
  "NAME",
  "USE",
  "DESCRIBE",
  "ENTER",
  "GUARD",
  "MESSAGE",
  "END",
  "RIDE",
  "FOLLOW",
  "RESEARCH",
  "GIVE",
  "HELP",
  "FIGHT",
  "COMBATMAGIC",
  "BUY",
  "CONTACT",
  "TEACH",
  "STUDY",
  "MAKE",
  "MOVE",
  "PASSWORD",
  "PLANT",
  "RECRUIT",
  "REPORT",
  "OPTION",
  "SPY",
  "SETSTEALTH",
  "CARRY",
  "QUIT",
  "TAX",
  "ENTERTAIN",
  "SELL",
  "LEAVE",
  "CAST",
  "SHOW",
  "DESTROY",
  "FORGET",
  "DEFAULT",
  "COMMENT",
  "ROUTE",
  "SABOTAGE",
  "BREED",
  "ORIGIN",
  "EMAIL",
  "RESERVE",
  "CLAIM",
  "BANNER",
  "NUMBER",
  "SCHOOL",
  "PIRACY",
  "RESTART",
  "GROUP",
  "SORT",
  "PREFIX",
  "PROMOTION",
  "PROMOTE",
  "LANGUAGE",
  "ORDERSTART",
  "NEXT",
  "LOCALE",
  "UNIT",
  "REGION"
};

typedef struct _keyword {
  struct _keyword *next;
  char *name;
  int keyword;
} t_keyword;

t_keyword *keywords = NULL;

#define igetkeyword(s)  findtoken(igetstr(s), UT_KEYWORD)

static char *magiegebiet[] = {
  "Illaun",
  "Tybied",
  "Cerddor",
  "Gwyrrd",
  "Draig"
};

enum {
  P_ALLES,
  P_EACH,
  P_PEASANT,
  P_CASTLE,
  P_BUILDING,
  P_UNIT,
  P_FLEE,
  P_REAR,
  P_FRONT,
  P_CONTROL,
  P_HERBS,
  P_TREES,
  P_NOT,
  P_FACTION,
  P_PERSON,
  P_REGION,
  P_SHIP,
  P_SILVER,
  P_ROAD,
  P_TEMP,
  P_PRIVAT,
  P_FIGHT,
  P_OBSERVE,
  P_GIVE,
  P_GUARD,
  P_FACTIONSTEALTH,
  P_WARN,
  P_LEVEL,
  P_HORSE,
  P_FOREIGN,
  P_AGGRESSIVE,
  P_DEFENSIVE,
  P_NUMBER,
  P_BEFORE,
  P_AFTER,
  P_ALLIANCE,
  P_KICK,
  P_LEAVE,
  P_NEW,
  P_INVITE,
  P_JOIN,
  P_AUTO,
  MAXPARAMS
};

static const char *Params[MAXPARAMS] = {
  "ALL",
  "EACH",
  "PEASANTS",
  "CASTLE",
  "BUILDING",
  "UNIT",
  "FLEE",
  "REAR",
  "FRONT",
  "CONTROL",
  "HERBS",
  "TREES",
  "NOT",
  "FACTION",
  "PERSON",
  "REGION",
  "SHIP",
  "SILVER",
  "ROAD",
  "TEMP",
  "PRIVATE",
  "FIGHT",
  "OBSERVE",
  "GIVE",
  "GUARD",
  "FACTIONSTEALTH",
  "WARN",
  "LEVEL",
  "HORSES",
  "FOREIGN",
  "AGGRESSIVE",
  "DEFENSIVE",
  "NUMBER",
  "BEFORE",
  "AFTER",
  "ALLIANCE",
  "KICK",
  "LEAVE",
  "NEW",
  "INVITE",
  "JOIN",
  "AUTO"
};

typedef struct _params {
  struct _params *next;
  char *name;
  int param;
} t_params;

t_params *parameters = NULL;

static char *reports[] = {      /* Fehler und Meldungen im Report */
  "Kampf",
  "Ereignisse",
  "Bewegung",
  "Einkommen",
  "Handel",
  "Produktion",
  "Orkvermehrung",
  "Alles"
};

static const int MAXREPORTS = sizeof(reports) / sizeof(reports[0]);

static char *message_levels[] = {
  "Wichtig",
  "Debug",
  "Fehler",
  "Warnungen",
  "Infos"
};

static const int ML_MAX = sizeof(message_levels) / sizeof(message_levels[0]);

t_liste *options = NULL;

typedef struct _direction {
  struct _direction *next;
  char *name;
  int dir;
} t_direction;

enum {
  D_EAST,
  D_WEST,
  D_PAUSE,
  D_NORTHEAST,
  D_NORTHWEST,
  D_SOUTHEAST,
  D_SOUTHWEST,
  MAXDIRECTIONS
};

static const char *Directions[] = {
  "EAST",
  "WEST",
  "PAUSE",
  "NORTHEAST",
  "NORTHWEST",
  "SOUTHEAST",
  "SOUTHWEST"
};

t_direction *directions = NULL;

t_liste *Rassen = NULL;

t_liste *shiptypes = NULL;

t_liste *herbdata = NULL;

enum {
  NULLPERSONSLEARN,
  NULLPERSONSTEACH,
  ONEATTACKPERUNIT,
  ONECARRYPERUNIT,
  ONEPERSONPERMAGEUNIT,
  ACTIVATED,
  ALREADYUSEDINLINE,
  AND,
  ASSUMING200STUDYCOSTS,
  AWARNING,
  BEFOREINCOME,
  BREEDHORSESORHERBS,
  BUILDINGNEEDSSILVER,
  BUTDOESNTLEARN,
  BUYALLNOTPOSSIBLE,
  CANTATTACKTEMP,
  CANTCHANGELOCALE,
  CANTDESCRIBEOBJECT,
  CANTENTEROBJECT,
  CANTFINDUNIT,
  CANTHANDLEPERSONCOMMENT,
  CANTHANDLESILVERCOMMENT,
  CANTMAINTAINBUILDING,
  CANTMAKETHAT,
  CANTREADFILE,
  CANTRENAMEOBJECT,
  CHECKYOURORDERS,
  COMBATSPELLSET,
  DELIVERYTO,
  DESTROYNOSTREET,
  DIRECTION,
  DISCOVERED,
  DOESNTCARRY,
  DOESNTRIDE,
  ECHECK,
  ENDWITHOUTTEMP,
  ERRORCOORDINATES,
  ERRORHELP,
  ERRORINLINE,
  ERRORLEVELPARAMETERS,
  ERRORNAMEFOREIGN,
  ERROROPINION,
  ERRORREGION,
  ERRORREGIONPARAMETER,
  ERRORSPELLSYNTAX,
  ERRORSURVEY,
  FACTION,
  FACTIONS,
  FACTION0USED,
  FACTIONINVALID,
  FACTIONMISSING,
  FOLLOW,
  FOUNDERROR,
  FOUNDERRORS,
  FOUNDORDERS,
  GIVEWHAT,
  INTERNALCHECK,
  INVALIDEMAIL,
  ISCOMBATSPELL,
  ISUSEDIN2REGIONS,
  ISNOTCOMBATSPELL,
  ITEM,
  LINETOOLONG,
  LOCALEMISMATCH,
  LONGCOMBATNOLONGORDER,
  LONGORDERMISSING,
  MAGIC,
  MAINTAINANCEMOVED,
  MISSINGFILES,
  MISSFILEPARAM,
  MISSFILECMD,
  MISSFILEITEM,
  MISSFILESKILL,
  MISSFILEDIR,
  MISSFILEMSG,
  MISSINGQUOTES,
  MISSINGDISGUISEPARAMETERS,
  MISSINGEND,
  MISSINGFACTIONNUMBER,
  MISSINGNEXT,
  MISSINGNUMRECRUITS,
  MISSINGOFFER,
  MISSINGPARAMETERS,
  MISSINGPASSWORD,
  MISSINGSTART,
  MISSINGUNITNUMBER,
  MOVENOTPOSSIBLEWITHPAUSE,
  MSGTO,
  NAMECONTAINSBRACKETS,
  NEEDBOTHCOORDINATES,
  NOCARRIER,
  NOFIND,
  NOLUXURY,
  NORMALUNITSONLY,
  NOSEND,
  NOSPACEHERE,
  NOTEMPNUMBER,
  NOTEXECUTED,
  NOTEXT,
  NOTFOUND,
  NTOOBIG,
  NUMBER0SENSELESS,
  NUMBEREXPECTED,
  NUMBERNOTPOSSIBLE,
  NUMCASTLEMISSING,
  NUMLUXURIESMISSING,
  NUMMISSING,
  OBJECTNUMBERMISSING,
  ONLYSABOTAGESHIP,
  ORDERNUMBER,
  ORDERSOK,
  ORDERSREAD,
  PASSWORDCLEARED,
  PASSWORDMSG1,
  PASSWORDMSG2,
  PASSWORDMSG3,
  POST,
  PRE,
  PROCESSINGFILE,
  QUITMSG,
  RECRUITCOSTSSET,
  REGIONMISSSILVER,
  RESEARCHHERBSONLY,
  RESERVE0SENSELESS,
  RESERVEDTOOMUCH,
  RESERVEWHAT,
  RESTARTMSG,
  RIDESWRONGUNIT,
  ROUTENOTCYCLIC,
  ROUTESTARTSWITHPAUSE,
  SCHOOLCHOSEN,
  SEARCHPATHIS,
  SILVERPOOL,
  SORT,
  SUPPLYISOBSOLETE,
  TEACHED,
  TEACHWHO,
  TEMPHASNTPERSONS,
  TEMPNOTTEMP,
  TEMPUNITSCANTRESERVE,
  TEMPUNITSCANTGIVE,
  TEXTTOOLONG,
  THERE,
  TOOMANYPARAMETERS,
  UNIT,
  UNITS,
  UNIT0NOTPOSSIBLE,
  UNIT0USED,
  UNITALREADYHAS,
  UNITALREADYHASLONGORDERS,
  UNITALREADYHASMOVED,
  UNITALREADYHASORDERS,
  UNITCANSTILLTEACH,
  UNITHASNTPERSONS,
  UNITHASPERSONS,
  UNITHASSILVER,
  UNITISTEACHED,
  UNITLOSESITEMS,
  UNITMISSCONTROL,
  UNITMISSING,
  UNITMISSPERSON,
  UNITMISSSILVER,
  UNITMOVESSHIP,
  UNITMOVESTOOFAR,
  UNITMUSTBEONSHIP,
  UNITNEEDSTEACHERS,
  UNITNOTONSHIPBUTONSHIP,
  UNITNOTPOSSIBLEHERE,
  UNITONSHIPHASMOVED,
  UNRECOGNIZEDDIRECTION,
  UNRECOGNIZEDOBJECT,
  UNRECOGNIZEDOPTION,
  UNRECOGNIZEDORDER,
  UNRECOGNIZEDPOTION,
  UNRECOGNIZEDRACE,
  UNRECOGNIZEDREPORTOPTION,
  UNRECOGNIZEDSCHOOL,
  UNRECOGNIZEDSKILL,
  UNRECOGNIZEDSPELL,
  USED1,
  USEEMAIL,
  USINGUNITINSTEAD,
  WARNINGS,
  WARNINGLEVEL,
  WARNINGLINE,
  WAS,
  WERE,
  WRONGFACTIONNUMBER,
  WRONGFIGHTSTATE,
  WRONGNUMBER,
  WRONGOUTPUTLEVEL,
  WRONGPARAMETER,
  MAX_ERRORS
};

static char *Errors[MAX_ERRORS] = {
  "0PERSONSLEARN",
  "0PERSONSTEACH",
  "1ATTACKPERUNIT",
  "1CARRYPERUNIT",
  "1PERSONPERMAGEUNIT",
  "ACTIVATED",
  "ALREADYUSEDINLINE",
  "AND",
  "ASSUMING200STUDYCOSTS",
  "AWARNING",
  "BEFOREINCOME",
  "BREEDHORSESORHERBS",
  "BUILDINGNEEDSSILVER",
  "BUTDOESNTLEARN",
  "BUYALLNOTPOSSIBLE",
  "CANTATTACKTEMP",
  "CANTCHANGELOCALE",
  "CANTDESCRIBEOBJECT",
  "CANTENTEROBJECT",
  "CANTFINDUNIT",
  "CANTHANDLEPERSONCOMMENT",
  "CANTHANDLESILVERCOMMENT",
  "CANTMAINTAINBUILDING",
  "CANTMAKETHAT",
  "CANTREADFILE",
  "CANTRENAMEOBJECT",
  "CHECKYOURORDERS",
  "COMBATSPELLSET",
  "DELIVERYTO",
  "DESTROYNOSTREET",
  "DIRECTION",
  "DISCOVERED",
  "DOESNTCARRY",
  "DOESNTRIDE",
  "ECHECK",
  "ENDWITHOUTTEMP",
  "ERRORCOORDINATES",
  "ERRORHELP",
  "ERRORINLINE",
  "ERRORLEVELPARAMETERS",
  "ERRORNAMEFOREIGN",
  "ERROROPINION",
  "ERRORREGION",
  "ERRORREGIONPARAMETER",
  "ERRORSPELLSYNTAX",
  "ERRORSURVEY",
  "FACTION",
  "FACTIONS",
  "FACTION0USED",
  "FACTIONINVALID",
  "FACTIONMISSING",
  "FOLLOW",
  "FOUNDERROR",
  "FOUNDERRORS",
  "FOUNDORDERS",
  "GIVEWHAT",
  "INTERNALCHECK",
  "INVALIDEMAIL",
  "ISCOMBATSPELL",
  "ISUSEDIN2REGIONS",
  "ISNOTCOMBATSPELL",
  "ITEM",
  "LINETOOLONG",
  "LOCALEMISMATCH",
  "LONGCOMBATNOLONGORDER",
  "LONGORDERMISSING",
  "MAGIC",
  "MAINTAINANCEMOVED",
  "MISSINGFILES",
  "MISSFILEPARAM",
  "MISSFILECMD",
  "MISSFILEITEM",
  "MISSFILESKILL",
  "MISSFILEDIR",
  "MISSFILEMSG",
  "MISSINGQUOTES",
  "MISSINGDISGUISEPARAMETERS",
  "MISSINGEND",
  "MISSINGFACTIONNUMBER",
  "MISSINGNEXT",
  "MISSINGNUMRECRUITS",
  "MISSINGOFFER",
  "MISSINGPARAMETERS",
  "MISSINGPASSWORD",
  "MISSINGSTART",
  "MISSINGUNITNUMBER",
  "MOVENOTPOSSIBLEWITHPAUSE",
  "MSGTO",
  "NAMECONTAINSBRACKETS",
  "NEEDBOTHCOORDINATES",
  "NOCARRIER",
  "NOFIND",
  "NOLUXURY",
  "NORMALUNITSONLY",
  "NOSEND",
  "NOSPACEHERE",
  "NOTEMPNUMBER",
  "NOTEXECUTED",
  "NOTEXT",
  "NOTFOUND",
  "NTOOBIG",
  "NUMBER0SENSELESS",
  "NUMBEREXPECTED",
  "NUMBERNOTPOSSIBLE",
  "NUMCASTLEMISSING",
  "NUMLUXURIESMISSING",
  "NUMMISSING",
  "OBJECTNUMBERMISSING",
  "ONLYSABOTAGESHIP",
  "ORDERNUMBER",
  "ORDERSOK",
  "ORDERSREAD",
  "PASSWORDCLEARED",
  "PASSWORDMSG1",
  "PASSWORDMSG2",
  "PASSWORDMSG3",
  "POST",
  "PRE",
  "PROCESSINGFILE",
  "QUITMSG",
  "RECRUITCOSTSSET",
  "REGIONMISSSILVER",
  "RESEARCHHERBSONLY",
  "RESERVE0SENSELESS",
  "RESERVEDTOOMUCH",
  "RESERVEWHAT",
  "RESTARTMSG",
  "RIDESWRONGUNIT",
  "ROUTENOTCYCLIC",
  "ROUTESTARTSWITHPAUSE",
  "SCHOOLCHOSEN",
  "SEARCHPATHIS",
  "SILVERPOOL",
  "SORT",
  "SUPPLYISOBSOLETE",
  "TEACHED",
  "TEACHWHO",
  "TEMPHASNTPERSONS",
  "TEMPNOTTEMP",
  "TEMPUNITSCANTRESERVE",
  "TEMPUNITSCANTGIVE",
  "TEXTTOOLONG",
  "THERE",
  "TOOMANYPARAMETERS",
  "UNIT",
  "UNITS",
  "UNIT0NOTPOSSIBLE",
  "UNIT0USED",
  "UNITALREADYHAS",
  "UNITALREADYHASLONGORDERS",
  "UNITALREADYHASMOVED",
  "UNITALREADYHASORDERS",
  "UNITCANSTILLTEACH",
  "UNITHASNTPERSONS",
  "UNITHASPERSONS",
  "UNITHASSILVER",
  "UNITISTEACHED",
  "UNITLOSESITEMS",
  "UNITMISSCONTROL",
  "UNITMISSING",
  "UNITMISSPERSON",
  "UNITMISSSILVER",
  "UNITMOVESSHIP",
  "UNITMOVESTOOFAR",
  "UNITMUSTBEONSHIP",
  "UNITNEEDSTEACHERS",
  "UNITNOTONSHIPBUTONSHIP",
  "UNITNOTPOSSIBLEHERE",
  "UNITONSHIPHASMOVED",
  "UNRECOGNIZEDDIRECTION",
  "UNRECOGNIZEDOBJECT",
  "UNRECOGNIZEDOPTION",
  "UNRECOGNIZEDORDER",
  "UNRECOGNIZEDPOTION",
  "UNRECOGNIZEDRACE",
  "UNRECOGNIZEDREPORTOPTION",
  "UNRECOGNIZEDSCHOOL",
  "UNRECOGNIZEDSKILL",
  "UNRECOGNIZEDSPELL",
  "USED1",
  "USEEMAIL",
  "USINGUNITINSTEAD",
  "WARNINGS",
  "WARNINGLEVEL",
  "WARNINGLINE",
  "WAS",
  "WERE",
  "WRONGFACTIONNUMBER",
  "WRONGFIGHTSTATE",
  "WRONGNUMBER",
  "WRONGOUTPUTLEVEL",
  "WRONGPARAMETER",
};

char *errtxt[MAX_ERRORS];

enum {
  HELP_CAPTION,
  HELP_PATH,
  HELP_TEXT,
  MAX_HELP
};

static char *Help[MAX_HELP] = {
  "CAPTION",
  "PATH",
  "TEXT"
};

t_liste *help = NULL;
char *help_caption = NULL, *help_path = NULL;

typedef struct _names {
  struct _names *next;
  char *txt;
} t_names;

typedef struct _item {
  struct _item *next;
  t_names *name;
  int preis;                    /* für Luxusgüter */
} t_item;

t_item *itemdata = NULL;

typedef struct _spell {
  struct _spell *next;
  char *name;
  int kosten;
  char typ;
} t_spell;

t_spell *spells = NULL;

typedef struct _skills {
  struct _skills *next;
  char *name;
  int kosten;
} t_skills;

t_skills *skilldata = NULL;

#define SP_ZAUBER 1
#define SP_KAMPF  2
#define SP_POST   4
#define SP_PRAE   8
#define SP_BATTLE 14            /* 2+4+8 */
#define SP_ALL    15

t_liste *potionnames = NULL;

t_liste *buildingtypes = NULL;

enum {
  POSSIBLE,
  NECESSARY
};

/*
 * ---------------------------------------------------------------------
 */

typedef struct list {
  struct list *next;
} list;

typedef struct t_region {
  struct t_region *next;
  int personen, geld, x, y, line_no, reserviert;
  char *name;
} t_region;

typedef struct unit {
  struct unit *next;
  int no;
  int people;
  int money;
  int reserviert;
  int unterhalt;
  int line_no;
  int temp;
  int ship;                     /* Nummer unseres Schiffes; ship<0: ich  bin owner */
  char lives, hasmoved;
  t_region *region;
  int newx, newy;
  int transport;                /* hier steht drin, welche Einheit mich  CARRYIEREn wird */
  int drive;                    /* hier steht drin, welche Einheit mich  CARRYIEREn soll */
  /*
   * wenn drive!=0, muß transport==drive sein, sonst irgendwo
   * Tippfehler
   */
  int start_of_orders_line;
  int long_order_line;
  char *start_of_orders;
  char *long_order;
  char *order;
  int schueler;
  int lehrer;
  int lernt;
  int spell;                    /* Bit-Map: 2^Spell-Typ */
} unit;

typedef struct teach {
  struct teach *next;
  unit *teacher;
  unit *student;
} teach;

teach *teachings = NULL;

static unit *units = NULL;

static unit *order_unit,               /* Die Einheit, die gerade dran ist */
*mother_unit,                   /* Die Einheit, die MACHE TEMP macht */
*cmd_unit;                      /* Die Einheit, die gerade angesprochen  wird, z.B. mit GIB */

t_region *Regionen = NULL;

typedef struct tnode {
  char c;
  bool leaf;
  int id;
  struct tnode *nexthash;
  struct tnode *next[32];
} tnode;

tnode tokens[UT_MAX];

unit *find_unit(int i, int t);
unit *newunit(int n, int t);
int findstr(char **v, const char *s, int max);

#define addlist(l, p)  (p->next=*l, *l=p)

int Pow(int p)
{
  if (!p)
    return 1;
  else
    return 2 << (p - 1);
}

#define iswxspace(c) (c==160 || iswspace(c))

char *eatwhite(char *ptr)
{
  int ret = 0;

  while (*ptr) {
    ucs4_t ucs;
    size_t size = 0;
    ret = unicode_utf8_to_ucs4(&ucs, ptr, &size);
    if (ret != 0 || !iswxspace((wint_t) ucs)) {
      break;
    }
    ptr += size;
  }
  return ptr;
}


const char *itob(int i)
{
  static char buffer[40];
  static int index = 0;
  char *dst;
  int b = i, x;

  if (i == 0)
    return "0";

  index = (index+1) % 4;
  dst = buffer + index * 10 + 9;

  *dst = 0;
  do {
    x = b % 36;
    b /= 36;
    dst--;
    if (x < 10)
      *dst = (char)('0' + x);
    else
      *dst = (char)('a' + (x - 10));
  }
  while (b > 0);
  return dst;
}

#define scat(X) strcat(checked_buf, X)
#define Scat(X) do { scat(" "); scat(X); } while (0)

void qcat(char *s)
{
  char *x;

  x = strchr(s, ' ');
  if (x != NULL && does_default == 0)
    scat(" \"");
  else
    scat(" ");
  scat(s);
  if (x != NULL && does_default == 0)
    scat("\"");
}

void icat(int n)
{
  static char s[10];

  sprintf(s, " %d", n);
  scat(s);
}

void bcat(int n)
{
  static char s[10];

  sprintf(s, " %s", itob(n));
  scat(s);
}

int ItemPrice(int i)
{
  static int ino = 0;
  static t_item *item = NULL, *it = NULL;
  int x;

  if (i < 0)
    return 0;
  if (!it)
    item = it = itemdata;
  if (i != ino) {
    x = i;
    if (i < ino)
      it = itemdata;
    else
      i -= ino;
    ino = x;
    while (0 < i-- && it)
      it = it->next;
    if (!it) {
      fprintf(ERR,
        "Interner Fehler: ItemPrice %d (%d) nicht gefunden!\n"
        "Internal Error: ItemPrice %d (%d) not found!\n", ino, i, ino, i);
      exit(123);
    }
    item = it;
  }
  return item->preis;
}

char *ItemName(int i, int plural)
{
  static int ino = 0;
  static t_item *item = NULL, *it = NULL;
  int x;

  if (!it)
    item = it = itemdata;
  if (i != ino) {
    x = i;
    if (i < ino)
      it = itemdata;
    else
      i -= ino;
    ino = x;
    while (0 < i-- && it)
      it = it->next;
    if (!it) {
      fprintf(ERR,
        "Interner Fehler: ItemName %d (%d) nicht gefunden!\n"
        "Internal Error: ItemName %d (%d) not found!\n", ino, i, ino, i);
      exit(123);
    }
    item = it;
  }
  if (plural)
    return item->name->next->txt;
  return item->name->txt;
}

FILE *path_fopen(const char *path_par, const char *file, const char *mode)
{
  char *pathw = strdup(path_par);
  char *token = strtok(pathw, PATH_DELIM);

  while (token != NULL) {
    char buf[1024];
    FILE *F;

    if (echeck_rules) {
      sprintf(buf, "%s/%s/%s/%s", token, echeck_rules, echeck_locale, file);
    } else {
      sprintf(buf, "%s/%s/%s", token, echeck_locale, file);
    }
    F = fopen(buf, mode);
    if (F != NULL) {
      free(pathw);
      return F;
    }
    token = strtok(NULL, PATH_DELIM);
  }
  free(pathw);
  return NULL;
}

char *transliterate(char *out, size_t size, const char *in)
{
  const char *src = in;
  char *dst = out;

  --size;                       /* need space for a final 0-byte */
  while (*src && size) {
    size_t len;
    const char *p = src;
    while ((p + size > src) && *src && (~*src & 0x80)) {
      *dst++ = (char)tolower(*src++);
    }
    len = src - p;
    size -= len;
    while (size > 0 && *src && (*src & 0x80)) {
      unsigned int advance = 2;
      if (src[0] == '\xc3') {
        if (src[1] == '\xa4' || src[1] == '\x84') {
          memcpy(dst, "ae", 2);
        } else if (src[1] == '\xb6' || src[1] == '\x96') {
          memcpy(dst, "oe", 2);
        } else if (src[1] == '\xbc' || src[1] == '\x9c') {
          memcpy(dst, "ue", 2);
        } else if (src[1] == '\x9f') {
          memcpy(dst, "ss", 2);
        } else {
          advance = 0;
        }
      } else if (src[0] == '\xe1') {
        if (src[1] == '\xba' && src[2] == '\x9e') {
          memcpy(dst, "ss", 2);
          ++src;
        } else {
          advance = 0;
        }
      } else {
        advance = 0;
      }

      if (advance && advance <= size) {
        src += advance;
        dst += advance;
        size -= advance;
      } else {
        ucs4_t ucs;
        unicode_utf8_to_ucs4(&ucs, src, &len);
        src += len;
        *dst++ = '?';
        --size;
      }
    }
  }
  *dst = 0;
  return *src ? 0 : out;
}

/** parsed einen String nach Zaubern */
void readspell(char *s)
{
  char *x;
  t_spell *sp;
  char buffer[128];

  sp = (t_spell *) calloc(1, sizeof(t_spell));
  x = strchr(s, ';');
  if (!x)
    x = strchr(s, ',');
  if (x)
    *x = 0;
  else {
    x = strchr(s, '\n');
    if (x)
      *x = 0;
    x = NULL;
  }
  sp->name = strdup(transliterate(buffer, sizeof(buffer), s));
  if (x) {
    s = eatwhite(x + 1);
    if (*s) {
      sp->kosten = atoi(s);
      x = strchr(s, ';');
      if (!x)
        x = strchr(s, ',');
      if (x) {
        s = (char *)(x + 1);
        if (*s)
          sp->typ = (char)Pow(atoi(s));
      }
    }
  }
  sp->next = spells;
  spells = sp;
}

void readskill(char *s)
{                               /* parsed einen String nach Talenten */
  char buffer[128];
  char *x;
  t_skills *sk;

  sk = (t_skills *) calloc(1, sizeof(t_skills));
  x = strchr(s, ';');
  if (!x)
    x = strchr(s, ',');
  if (x)
    *x = 0;
  else {
    x = strchr(s, '\n');
    if (x)
      *x = 0;
    x = NULL;
  }
  sk->name = strdup(transliterate(buffer, sizeof(buffer), s));
  if (x) {
    s = (char *)(x + 1);
    while (isspace(*s))
      s++;
    if (*s)
      sk->kosten = atoi(s);
  }
  sk->next = skilldata;
  skilldata = sk;
}

int readitem(char *s)
{                               /* parsed einen String nach Items */
  char buffer[128];
  char *x = s;
  t_item *it = (t_item *) calloc(1, sizeof(t_item));

  do {
    x = strchr(s, ';');
    if (!x)
      x = strchr(s, ',');
    if (x)
      *x = 0;
    else {
      x = strchr(s, '\n');
      if (x)
        *x = 0;
      x = NULL;
    }
    if (atoi(s) > 0)            /* Name, 12 -> Luxusgut "Name", EK-Preis *   12 */
      it->preis = atoi(s);
    else {
      t_names *n = (t_names *) calloc(1, sizeof(t_names));
      n->txt = strdup(transliterate(buffer, sizeof(buffer), s));
      n->next = it->name;
      it->name = n;
    }
    if (x) {
      s = eatwhite(x + 1);
    }
  }
  while (x && *s);
  if (!it->name) {
      free(it);
      return 0;
  }
  it->next = itemdata;
  itemdata = it;
  return 1;
}

void readliste(char *s, t_liste ** L)
{                               /* parsed einen String nach einem Token */
  char *x;
  t_liste *ls;
  char buffer[128];

  ls = (t_liste *) calloc(1, sizeof(t_liste));
  x = strchr(s, '\n');
  if (x)
    *x = 0;
  ls->name = strdup(transliterate(buffer, sizeof(buffer), s));
  addlist(L, ls);
}

int readkeywords(char *s)
{                               /* parsed einen String nach Befehlen */
  char *x;
  t_keyword *k;
  int i;
  char buffer[64];

  x = strchr(s, ';');
  if (!x)
    x = strchr(s, ',');
  if (x)
    *x = 0;
  else
    return 0;

  for (i = 0; i < MAXKEYWORDS; i++) {
    if (stricmp(s, Keywords[i]) == 0) {
      break;
    }
  }
  if (i == MAXKEYWORDS) {
    fprintf(stderr, "Unknown keyword '%s'\n", s);
    return 0;
  }
  s = eatwhite(x + 1);
  if (!*s)
    return 0;
  x = strchr(s, '\n');
  if (x)
    *x = 0;
  k = (t_keyword *) calloc(1, sizeof(t_keyword));
  k->name = strdup(transliterate(buffer, sizeof(buffer), s));
  k->keyword = i;
  k->next = keywords;
  keywords = k;
  return 1;
}

int readparams(char *s)
{                               /* parsed einen String nach Parametern */
  char buffer[128];
  char *x;
  int i;
  t_params *p;

  x = strchr(s, ';');
  if (!x)
    x = strchr(s, ',');
  if (x)
    *x = 0;
  else
    return 0;
  for (i = 0; i < MAXPARAMS; i++)
    if (stricmp(s, Params[i]) == 0)
      break;
  if (i == MAXPARAMS)
    return 0;
  s = eatwhite(x + 1);
  if (!*s)
    return 0;
  x = strchr(s, '\n');
  if (x)
    *x = 0;
  p = (t_params *) calloc(1, sizeof(t_params));
  p->name = strdup(transliterate(buffer, sizeof(buffer), s));
  p->param = i;
  p->next = parameters;
  parameters = p;
  return 1;
}

int readdirection(char *s)
{                               /* parsed einen String nach Richtungen */
  char buffer[128];
  char *x;
  t_direction *d;
  int i;

  x = strchr(s, ';');
  if (!x)
    x = strchr(s, ',');
  if (x)
    *x = 0;
  else
    return 0;

  for (i = 0; i < MAXDIRECTIONS; i++)
    if (stricmp(s, Directions[i]) == 0)
      break;
  if (i == MAXDIRECTIONS)
    return 0;
  s = eatwhite(x + 1);
  if (!*s)
    return 0;
  x = strchr(s, '\n');
  if (x)
    *x = 0;
  d = (t_direction *) calloc(1, sizeof(t_direction));
  d->name = strdup(transliterate(buffer, sizeof(buffer), s));
  d->dir = i;
  d->next = directions;
  directions = d;
  return 1;
}

int readerror(char *s)
{
  int i;
  char *x;

  x = strchr(s, ',');
  if (!x)
    return 0;
  *x = 0;
  i = findstr(Errors, s, MAX_ERRORS);
  if (i < 0)
    return 0;
  x = eatwhite(x + 1);
  if (!(*x) || *x == '\n')
    return 0;
  s = x;
  while (*x == '~') {
    *x = ' ';
    x++;
  }
  do {
    x = strchr(x, '\\');
    if (!x)
      break;
    *x = '\n';                  /* nur \n erlaubt... */
    x++;
    memmove(x, x + 1, strlen(x));       /* Rest ein Zeichen "ranziehen" */
  }
  while (*x);
  errtxt[i] = strdup(s);
  filesread |= HAS_MESSAGES;
  return 1;
}

int readhelp(char *s)
{
  int i;
  char *x;
  t_liste *h;

  x = strchr(s, ',');
  if (!x)
    return 0;
  *x = 0;
  x++;
  if (!(*x))
    return 0;
  x++;                          /* erstes Leerzeichen hinter dem ,  überlesen */
  if (!(*x))
    return 0;
  i = findstr(Help, s, MAX_HELP);
  if (i < 0)
    return 0;
  s = x;
  do {
    s = strchr(s, '\\');
    if (!s)
      break;
    *s = '\n';                  /* nur \n erlaubt... */
    s++;
    memmove(s, s + 1, strlen(s));       /* Rest ein Zeichen "ranziehen" */
  }
  while (*s);

  switch (i) {
  case HELP_CAPTION:
    help_caption = strdup(x);
    break;
  case HELP_PATH:
    help_path = strdup(x);
    break;
  default:
    h = (t_liste *) calloc(1, sizeof(t_liste));
    h->name = strdup(x);
    addlist(&help, h);
    break;
  }
  return 1;
}

int parsefile(char *s, int typ)
{                               /* ruft passende Routine auf */
  int ok;                       /* nicht alle Zeilenparser geben Wert  zurück */
  char *x, *y;
  int i;

  ok = 1;
  switch (typ) {
  case UT_PARAM:
    ok = readparams(s);
    if (ok)
      filesread |= HAS_PARAM;
    break;

  case UT_ITEM:
    ok = readitem(s);
    if (ok)
      filesread |= HAS_ITEM;
    break;

  case UT_SKILL:
    readskill(s);
    filesread |= HAS_SKILL;
    break;

  case UT_KEYWORD:
    ok = readkeywords(s);
    if (ok)
      filesread |= HAS_KEYWORD;
    break;

  case UT_BUILDING:
    readliste(s, &buildingtypes);
    break;

  case UT_HERB:
    readliste(s, &herbdata);
    break;

  case UT_POTION:
    readliste(s, &potionnames);
    break;

  case UT_RACE:
    readliste(s, &Rassen);
    break;

  case UT_SPELL:
    readspell(s);
    break;

  case UT_SHIP:
    readliste(s, &shiptypes);
    break;

  case UT_OPTION:
    readliste(s, &options);
    break;

  case UT_DIRECTION:
    ok = readdirection(s);
    if (ok)
      filesread |= HAS_DIRECTION;
    break;

  case UT_ERRORS:
    ok = readerror(s);
    break;

  case UT_HELP:
    ok = readhelp(s);
    break;

  default:                     /* tokens.txt */
    x = strchr(s, ':');         /* TOKEN: werte */
    if (!x)
      return 0;
    y = x + 1;
    while (isspace(*(x - 1)))
      x--;
    *x = 0;
    for (i = 1; i < UT_MAX; i++)
      if (strcmp(s, Keys[i]) == 0)
        break;
    if (i == UT_MAX)
      return 0;
    y = eatwhite(y);
    if (*y && (*y == '#' || *y == '\n'))
      return 1;
    return parsefile(y, i);
  }
  return ok;
}

#ifdef MACOSX
static void macify(unsigned char *s)
{

  unsigned char translate[][2] = {
    {128, 196},                 /* Adieresis */
    {133, 214},                 /* Odieresis */
    {134, 220},                 /* Udieresis */
    {167, 223},                 /* germandbls */
    {138, 228},                 /* adieresis */
    {154, 246},                 /* odieresis */
    {159, 252},                 /* udieresis */
    {0, 0}
  };
  for (; *s; ++s) {
    int i = 0;

    while (translate[i][1] > *s)
      ++i;
    if (translate[i][1] == *s)
      *s = translate[i][0];
  }
}
#endif

static char *mocked_input = 0;
static char *mock_pos = 0;

void set_order_unit(unit * u)
{
  order_unit = u;
}

void set_output(FILE *out, FILE *err) {
  if (out)
    OUT = out;
  if (err)
    ERR = err;
}

#ifdef WITH_CUTEST

void mock_input(const char *input)
{
  free(mocked_input);
  mocked_input = strdup(input);
  mock_pos = mocked_input;
}

int get_long_order_line() {
  if (order_unit)
    return order_unit->long_order_line;
  else
    return -1;
}
#endif

static char *fgetbuffer(char *buf, int size, FILE * F)
{
  if (mocked_input) {
    size_t bytes;
    char *nextbr;
    if (!*mock_pos)
      return 0;
    nextbr = strchr(mock_pos, '\n');
    if (!nextbr) {
      nextbr = mock_pos + strlen(mock_pos);
    } else {
      ++nextbr;
    }
    bytes = size - 1;
    if (bytes > nextbr - mock_pos) {
      bytes = nextbr - mock_pos;
    }
    if (bytes)
      memcpy(buf, mock_pos, bytes);
    buf[bytes] = 0;
    mock_pos += bytes;
    return buf;
  }
  return fgets(buf, size, F);
}

void readafile(const char *fn, int typ)
{
  int ok, line;
  FILE *F;
  char *s, *x;

  F = path_fopen(path, fn, "r");
  if (!F)
    return;
  for (line = 1;; line++) {
    do {
      s = fgets(order_buf, MAXLINE, F);
    }
    while (!feof(F) && s && (*s == '#' || *s == '\n')); /* Leer- und  Kommentarzeilen   überlesen */
    if (feof(F) || !s) {
      fclose(F);
      return;
    }
#ifdef MACOSX
    macify(s);
#endif
    x = strchr(s, '\n');
    if (x)
      *x = 0;                   /* \n am Zeilenende löschen */
    ok = parsefile(s, typ);
    if (!ok)
      fprintf(ERR, "Fehler in Datei %s Zeile %d: `%s'\n"
        "Error in file %s line %d: `%s'\n", fn, line, s, fn, line, s);
  }
}

void readfiles(int doall)
{                               /* liest externen Files */
  int i;

  if (doall)
    /*
     * alle Files aus der Liste der Reihe nach zu lesen versuchen
     */
    for (i = 0; i < filecount; i++)
      readafile(ECheck_Files[i].name, ECheck_Files[i].type);
  else
    /*
     * nur die Help-Files und tokens.txt
     */
    for (i = 0; i < filecount; i++)
      if (ECheck_Files[i].type == UT_HELP || ECheck_Files[i].type < 0)
        readafile(ECheck_Files[i].name, ECheck_Files[i].type);
}

void porder(void)
{
  int i;

  if (echo_it) {
    if (does_default != 2)
      for (i = 0; i != indent; i++)
        putc(' ', OUT);

    if (bang_cmd)
      putc('!', OUT);
    bang_cmd = 0;
    if (at_cmd)
      putc('@', OUT);
    at_cmd = 0;
    if (does_default > 0)
      fprintf(OUT, "%s%s", checked_buf, does_default == 2 ? "\"\n" : "");
    else {
      fputs(checked_buf, OUT);
      putc('\n', OUT);
    }
  }
  checked_buf[0] = 0;
  if (next_indent != indent)
    indent = next_indent;
}

void Error(char *text, int line, char *order)
{
  char bf[65];

  if (!order)
    strcpy(bf, "<--FEHLT!--MISSING!-->");
  else
    strncpy(bf, order, 64);
  strcpy(bf + 61, "...");       /* zu lange Befehle ggf. kürzen */
  error_count++;
  if (!brief) {
    switch (compile) {
    case OUT_NORMAL:
      fprintf(ERR, "%s %d: %s.\n  `%s'\n", errtxt[ERRORINLINE], line, text, bf);
      break;
    case OUT_COMPILE:
      fprintf(ERR, "%s(%d)|0|%s. `%s'\n", filename, line, text, bf);
      break;
    case OUT_MAGELLAN:
      fprintf(ERR, "%s|%d|0|%s. `%s'\n", filename, line, text, bf);
      break;
    }
  }
}

#define anerror(s)  Error(s, line_no, order_buf)

int btoi(char *s)
{
  static char buf[5];
  int n = 0;
  int i = 0;

  assert(s);
  if (!(*s))
    return 0;

  for(n = 0; n < 5 && isalnum(*s); ++n)
    buf[n] = *(s++);
  if (n > 4) {
    sprintf(message_buf, "%s: `%s'. %s", errtxt[NTOOBIG], s, errtxt[USED1]);
    anerror(message_buf);
    return 1;
  }
  buf[n] = 0;
  s = buf;
#ifdef HAVE_STRTOL
  i = (int)(strtol(s, NULL, 36));
#else
  while (isalnum(*s)) {
    i *= 36;
    if (isupper(*s))
      i += (*s) - 'A' + 10;
    else if (islower(*s))
      i += (*s) - 'a' + 10;
    else if (isdigit(*s))
      i += (*s) - '0';
    ++s;
  }
#endif
  return i;
}

const char *uid(unit * u)
{
  static char buffer[18*4];
  char *dst;
  static int index = 0;

  index = (index+1) % 4;
  dst = buffer + index * 18;

  sprintf(dst, "%s%s", u->temp != 0 ? "TEMP " : "", itob(u->no));
  return dst;
}

const char *Uid(int i)
{
  static char bf[18];
  unit *u;

  u = find_unit(i, 0);
  if (!u)
    u = find_unit(i, 1);
  if (!u) {
    sprintf(warn_buf, errtxt[CANTFINDUNIT], itob(i));
    Error(warn_buf, line_no, errtxt[INTERNALCHECK]);
    u = newunit(-1, 0);
  }
  sprintf(bf, "%s%s", u->temp != 0 ? "TEMP " : "", itob(u->no));
  return bf;
}

void warn(char *s, int line, char level)
{
  if (warn_off)
    return;
  if (level > show_warnings)
    return;
  warning_count++;
  if (show_warnings && !brief) {
    switch (compile) {
    case OUT_NORMAL:
      fprintf(ERR, "%s %d: %s\n", errtxt[WARNINGLINE], line, s);
      break;
    case OUT_COMPILE:
      fprintf(ERR, "%s(%d)|%d|%s\n", filename, line, level, s);
      break;
    case OUT_MAGELLAN:
      fprintf(ERR, "%s|%d|%d|%s\n", filename, line, level, s);
      break;
    }
  }
}

void warning(char *s, int line, char *order, char level)
{
  char bf[65];

  strncpy(bf, order, 64);
  strcpy(bf + 61, "...");       /* zu lange Befehle ggf. kürzen */
  if (warn_off)
    return;
  if (level > show_warnings)
    return;
  warning_count++;
  if (show_warnings && !brief) {
    switch (compile) {
    case OUT_NORMAL:
      fprintf(ERR, "%s %d: %s.\n  `%s'\n", errtxt[WARNINGLINE], line, s, bf);
      break;
    case OUT_COMPILE:
      fprintf(ERR, "%s(%d)%d|%s. `%s'\n", filename, line, level, s, bf);
      break;
    case OUT_MAGELLAN:
      fprintf(ERR, "%s|%d|%d|%s. `%s'\n", filename, line, level, s, bf);
      break;
    }
  }
}

#define awarning(s, level) warning(s, line_no, order_buf, level)

void checkstring(char *s, size_t l, int type)
{
  if (l > 0 && strlen(s) > l) {
    sprintf(warn_buf, errtxt[TEXTTOOLONG], (int)(l));
    awarning(warn_buf, 2);
  }
  if (s[0] == 0 && type == NECESSARY)
    awarning(errtxt[NOTEXT], 1);

  strncpy(warn_buf, s, l);

  if (echo_it && s[0]) {
    if (strlen(s) + strlen(checked_buf) > MARGIN) {
      qcat(s);
    } else
      qcat(s);
  }
}

int current_temp_no;

  /*
   * Enthält die TEMP No, falls eine TEMP Einheit angesprochen wurde.
   */

int from_temp_unit_no;

  /*
   * Enthält die TEMP No, falls Befehle von einer TEMP Einheit gelesen
   * werden.
   */

#define val(x)  (x!=0)

unit *find_unit(int i, int t)
{
  unit *u;

  for (u = units; u; u = u->next)
    if ((i < 0 && u->no < 0) || (u->no == i && val(u->temp) == val(t)))
      break;
  return u;
}

t_region *addregion(int x, int y, int pers)
{
  t_region *r, *R;

  for (r = Regionen; r; r = r->next)
    if (x == r->x && y == r->y)
      break;

  if (!r) {
    r = (t_region *) calloc(1, sizeof(t_region));
    r->x = x;
    r->y = y;
    r->personen = pers;
    r->geld = 0;
    r->reserviert = 0;
    r->name = strdup("Region");
    r->line_no = line_no;
    if (Regionen) {
      for (R = Regionen; R->next; R = R->next) ;        /* letzte Region der Liste  */
      R->next = r;              /* Region hinten dranklemmen */
    } else
      Regionen = r;
  } else
    r->personen += pers;
  return r;
}

void addteach(unit * teacher, unit * student)
{
  teach *t, **teachlist = &teachings;

  for (t = teachings; t; t = t->next) {
    if (t->student == student) {
      if (!teacher)             /* Aufruf durch Schüler, aber Lehrer hat  schon Struktur angelegt */
        return;
      if (t->teacher == teacher)        /* kann eigentlich nicht *  vorkommen, aber egal */
        return;
      if (t->teacher == NULL) { /* Schüler hat Struct angelegt (mit  unbek. Lehrer), wir tragen jetzt nur  noch den Lehrer nach */
        t->teacher = teacher;
        return;
      }
    }
  }
  t = (teach *) malloc(sizeof(teach));
  t->next = NULL;
  t->teacher = teacher;
  t->student = student;
  addlist(teachlist, t);
}

unit *newunit(int n, int t)
{
  unit *u = find_unit(n, t), *c;

  if (!u) {
    u = (unit *) calloc(1, sizeof(unit));
    u->no = n;
    u->line_no = line_no;
    u->order = strdup(order_buf);
    u->region = addregion(Rx, Ry, 0);
    u->newx = Rx;
    u->newy = Ry;
    u->temp = t;
    if (units) {
      for (c = units; c->next; c = c->next) ;   /* letzte unit der Liste */
      c->next = u;              /* unit hinten dranklemmen */
    } else
      units = u;
  }

  if (u->temp < 0) {
    sprintf(warn_buf, errtxt[ISUSEDIN2REGIONS], itob(u->no), Rx, Ry,
      u->newx, u->newy, u->start_of_orders_line);
    anerror(warn_buf);
    u->long_order_line = 0;
  }

  if (u->temp < 42)
    u->temp = t;

  if (t)
    current_temp_no = n;
  else
    current_temp_no = 0;

  return u;
}

int atoip(char *s)
{
  int n;

  n = atoi(s);
  if (n < 0)
    n = 0;
  return n;
}

char *igetstr(char *s1)
{
  int i;
  static char *s;
  static char buf[BUFSIZE];

  if (s1)
    s = s1;
  while (*s == SPACE)
    s++;

  for (i = 0; *s && *s != SPACE && i+1 < (int)sizeof(buf); i++, s++) {
    buf[i] = *s;

    if (*s == SPACE_REPLACEMENT) {
      if (i > 0 && buf[i - 1] == ESCAPE_CHAR)
        buf[--i] = SPACE_REPLACEMENT;
      else
        buf[i] = SPACE;
    }
  }
  buf[i] = 0;
  if (i > 0 && buf[i - 1] == ';')
    /*
     * Steht ein Semikolon direkt hinten dran, dies löschen
     */
    buf[i - 1] = 0;

  return buf;
}

char *printkeyword(int key)
{
  t_keyword *k = keywords;

  while (k && k->keyword != key) {
    k = k->next;
  }
  return (k && k->keyword == key) ? k->name : 0;
}

char *printdirection(int dir)
{
  t_direction *d = directions;

  while (d && d->dir != dir) {
    d = d->next;
  }
  return d ? d->name : 0;
}

char *printparam(int par)
{
  t_params *p = parameters;

  while (p && p->param != par)
    p = p->next;
  return p ? p->name : 0;
}

char *printliste(int key, t_liste * L)
{
  t_liste *l;

  for (l = L; key; l = l->next, key--) ;
  return l->name;
}

#define getstr()  igetstr(NULL)
#define getb()  btoi(igetstr(NULL))
#define geti()  atoi(igetstr(NULL))
#define getI()  btoi(igetstr(NULL))

int findstr(char **v, const char *s, int max)
{
  int i;
  size_t ss = strlen(s);

  if (!s[0])
    return -1;
  for (i = 0; i < max; i++)
    if (v[i] && unicode_utf8_strncasecmp(s, v[i], ss) == 0)
      return i;
  return -1;
}

int findtoken(const char *token, int type)
{
  tnode *tk = &tokens[type];
  char buffer[1024];
  const char *str;

  if (!(*token))
    return -1;

  str = transliterate(buffer, sizeof(buffer), token);

  if (str) {
    if (type==UT_KEYWORD) {
      at_cmd = 0;
      bang_cmd = 0;
      for (; *str; ++str) {
        if (*str=='!') bang_cmd = 1;
        else if (*str=='@') at_cmd = 1;
        else break;
      }
      if ((at_cmd || bang_cmd) && *str < 65) {
        anerror(errtxt[NOSPACEHERE]);
        return -2;
      }
    }
    while (*str) {
      char c = (char)tolower((unsigned char)*str);

      tk = tk->next[((unsigned char)c) % 32];
      while (tk && tk->c != c) {
        tk = tk->nexthash;
      }
      if (!tk) {
        return -1;
      }
      ++str;
    }
  }
  if (tk->id >= 0)
    return tk->id;
  else
    return -1;
}


int findparam(const char *s)
{
  int p;

  if (!*s)
    return -1;
  p = findtoken(s, UT_PARAM);
  if (p != -1)
    return p;
  p = findtoken(s, UT_BUILDING);
  if (p != -1)
    return P_BUILDING;
  return -1;
}

int isparam(char *s, int i, char print)
{
  if (i != findparam(s))
    return 0;
  if (print) {
    Scat(printparam(i));
  }
  return 1;
}

t_spell *findspell(char *s)
{
  t_spell *sp;

  if (!s[0] || !spells)
    return NULL;
  for (sp = spells; sp; sp = sp->next)
    if (sp->name && !unicode_utf8_strncasecmp(sp->name, s, strlen(s)))
      return sp;
  return NULL;
}

t_skills *findskill(char *s)
{
  int i;
  t_skills *sk = skilldata;

  i = findtoken(s, UT_SKILL);
  if (i < 0)
    return NULL;
  while (i--)
    sk = sk->next;
  return sk;
}

#define findherb(s) findtoken(s, UT_HERB)
#define findpotion(s) findtoken(s, UT_POTION)
#define finditem(s) findtoken(s, UT_ITEM)
#define findbuildingtype(s) findtoken(s, UT_BUILDING)
#define findreport(s)   findstr(reports, s, MAXREPORTS)
#define findoption(s)   findtoken(s, UT_OPTION)
#define findshiptype(s)   findtoken(s, UT_SHIP);
#define getskill()  findskill(getstr())
#define getparam()  findparam(getstr())
#define igetparam(s)  findparam(igetstr(s))

int finddirection(char *s)
{
  if (!*s)
    return -2;
  if (strcmp(s, "//") == 0)     /* "NACH NW NO NO // nach Xontormia" ist *   erlaubt */
    return -2;
  return findtoken(s, UT_DIRECTION);
}

#define getdirection()  finddirection(getstr())

#define getoption() findoption(getstr())

#define findkeyword(s)  findtoken(s, UT_KEYWORD)

char *getbuf(void)
{
  char lbuf[MAXLINE];
  bool cont = false;
  char quote = 0;
  bool report = false;
  char *cp = warn_buf;

  lbuf[MAXLINE - 1] = '@';

  do {
    bool eatwhite = true;
    bool start = true;
    char *end;
    char *bp = fgetbuffer(lbuf, MAXLINE, F);

    if (!bp)
      return NULL;

    end = bp + strlen(bp);
    if (end == bp || *(end - 1) == '\n') {
      line_no++;
      *(--end) = 0;
    } else {
      /*
       * wenn die Zeile länger als erlaubt war, wird der Rest
       * weggeworfen:
       */
      while (bp && !lbuf[MAXLINE - 1] && lbuf[MAXLINE - 2] != '\n')
        bp = fgetbuffer(warn_buf, 1024, F);
      sprintf(warn_buf, "%.30s", lbuf);
      Error(errtxt[LINETOOLONG], line_no, warn_buf);
      bp = lbuf;
    }
    cont = false;
    while (cp != warn_buf + MAXLINE && bp != lbuf + MAXLINE && *bp) {
      if (isspace(*bp)) {
        if (eatwhite) {
          do {
            ++bp;
          }
          while (bp != lbuf + MAXLINE && isspace(*bp));
          if (!quote && !start)
            *(cp++) = ' ';
        } else {
          do {
            *(cp++) = SPACE_REPLACEMENT;
            ++bp;
          }
          while (cp != warn_buf + MAXLINE && bp != lbuf + MAXLINE
            && isspace(*bp));
        }
      } else {
        if ((!quote || quote == *bp) && (*bp == DOUBLE_QUOTE || *bp == SINGLE_QUOTE)) {
          if (!cont && !quote) {
            quote = *bp;
            eatwhite = true;
          } else if (!cont && quote == *bp) {
            quote = 0;
            while(cp >= warn_buf && (*(cp-1) == SPACE_REPLACEMENT)) {
              --cp;
            }
            eatwhite = true;
          } else {
            *(cp++) = *bp;
          }
          cont = false;
        } else {
          if (*bp == '\\') {
            cont = true;
          } else if (!iscntrl(*bp)) {
            *(cp++) = *bp;
            eatwhite = (bool) ! quote;
            cont = false;
          }
        }
        ++bp;
      }
      start = false;
    }
    if (cp == warn_buf + MAXLINE) {
      --cp;
      if (!report) {
        report = true;
        sprintf(lbuf, "%.30s", warn_buf);
        Error(errtxt[LINETOOLONG], line_no, lbuf);
      }
    }
    *cp = 0;
  }
  while (cont || cp == warn_buf);

  if (quote)
    Error(errtxt[MISSINGQUOTES], line_no, lbuf);

  return warn_buf;
}

void get_order(void)
{
  char *buf;
  bool ok = false;

  while (!befehle_ende && !ok) {
    buf = getbuf();

    if (buf) {
      if (buf[0] == COMMENT_CHAR && buf[1] == COMMENT_CHAR) {
        if (ignore_NameMe)
          line_no--;
        continue;
      }
      strcpy(order_buf, buf);
      ok = true;
    } else
      befehle_ende = 1;
  }
}

void getafaction(char *s)
{
  int i;

  i = btoi(s);
  if (!s[0])
    anerror(errtxt[FACTIONMISSING]);
  else {
    if (!i)
      awarning(errtxt[FACTION0USED], 1);
    icat(i);
  }
}

int getmoreunits(bool partei)
{
  char *s;
  int i, count, temp;
  unit *u;

  count = 0;
  for (;;) {
    s = getstr();
    if (!(*s))
      break;

    if (partei) {
      i = btoi(s);
      if (i < 1) {
        sprintf(warn_buf, errtxt[FACTIONINVALID], s);
        anerror(warn_buf);
      } else
        bcat(i);
    } else {
      if (findparam(s) == P_TEMP) {
        scat(" TEMP");
        temp = 1;
        s = getstr();
      } else
        temp = 0;
      i = btoi(s);

      if (!i) {
        sprintf(warn_buf, errtxt[UNITNOTPOSSIBLEHERE], s);
        anerror(warn_buf);
      } else {
        bcat(i);
        if (!does_default) {
          u = newunit(i, temp);
          addteach(order_unit, u);
        }
      }
    }
    count++;
  }
  return count;
}

int getaunit(int type)
{
  char *s, is_temp = 0;
  int i;

  current_temp_no = 0;
  cmd_unit = NULL;

  s = getstr();
  if (s[0] == 0) {
    if (type == NECESSARY) {
      anerror(errtxt[UNITMISSING]);
      return 0;
    }
    return 1;
  }
  switch (findparam(s)) {
  case P_PEASANT:
    Scat(printparam(P_PEASANT));
    this_unit = 0;
    return 2;

  case P_TEMP:
    scat(" TEMP");
    s = getstr();
    current_temp_no = i = btoi(s);
    is_temp = 1;
    break;

  default:
    i = btoi(s);
    break;
  }

  if (type == 42) {             /* Nur Test, ob eine Einheit kommt, weil  das ein Fehler ist */
    if (i)
      return 42;
    return 0;
  }

  this_unit = i;

  cmd_unit = newunit(i, is_temp);       /* Die Unit schon machen, wegen  TEMP-Check */
  bcat(i);
  if (is_temp)
    return 3;
  return 1;
}

void copy_unit(unit * from, unit * to)
{
  to->no = from->no;
  to->people = from->people;
  to->money = from->money;
  to->line_no = from->line_no;
  to->temp = from->temp;
  to->lives = from->lives;
  to->start_of_orders_line = from->start_of_orders_line;
  to->long_order_line = from->long_order_line;
  if (from->start_of_orders_line) {
    free(to->start_of_orders);
    to->start_of_orders = strdup(from->start_of_orders);
  }
  if (from->long_order_line) {
    free(to->long_order);
    to->long_order = strdup(from->long_order);
  }
  if (from->order) {
    free(to->order);
    to->order = strdup(from->order);
  }
  to->lernt = from->lernt;
}

void expect_last(int level)
{
  char *s = getstr();
  if (s && s[0]) {
    if (level < 0)
      anerror(errtxt[TOOMANYPARAMETERS]);
    else
      awarning(errtxt[TOOMANYPARAMETERS], level);
  }
}


void checkemail(void)
{
  char *s, *addr;
  char bf[200];

  scat(printkeyword(K_EMAIL));
  addr = getstr();
  checkstring(addr, DESCRIBESIZE, NECESSARY);

  if (!addr) {
    awarning(errtxt[USEEMAIL], 3);
    sprintf(bf, "; %s!", errtxt[USEEMAIL]);
    scat(bf);
    return;
  }
  s = strchr(addr, '@');
  if (!s) {
    anerror(errtxt[INVALIDEMAIL]);
    return;
  }
  scat(errtxt[DELIVERYTO]);
  Scat(addr);
}

/*
 * Check auf langen Befehl usw. - aus ACheck.c 4.1
 */

void end_unit_orders(void)
{
  if (!order_unit)              /* Für den ersten Befehl der ersten *  Einheit. */
    return;

  if (order_unit->lives > 0 && !order_unit->long_order_line
    && order_unit->people > 0) {
    sprintf(warn_buf, errtxt[LONGORDERMISSING], uid(order_unit));
    warning(warn_buf, order_unit->start_of_orders_line,
      order_unit->start_of_orders, 2);
  }
}

void orders_for_unit(int i, unit * u)
{
  unit *t;
  char *k, *j, *e;
  int s;

  /* FIXME last unit is not checked for long orders*/
  end_unit_orders();
  set_order_unit(mother_unit = u);

  if (u->start_of_orders_line) {
    sprintf(warn_buf, errtxt[UNITALREADYHASORDERS],
      uid(u), u->start_of_orders_line);
    do {
      i++;
      if (i < 1)
        i = 1;
      u = newunit(i, 0);
    }
    while (u->start_of_orders_line);
    strcat(warn_buf, errtxt[USINGUNITINSTEAD]);
    strcat(warn_buf, itob(i));
    awarning(warn_buf, 1);
    set_order_unit(u);
  }

  u->start_of_orders = strdup(order_buf);
  u->start_of_orders_line = line_no;
  u->lives = 1;

  if (no_comment > 0)
    return;

  k = strchr(order_buf, '[');
  if (!k) {
    awarning(errtxt[CANTHANDLEPERSONCOMMENT], 4);
    no_comment++;
    return;
  }
  k++;
  while (!atoi(k)) {            /* Hier ist eine [ im Namen; 0 Personen  ist nicht in der Zugvorlage */
    k = strchr(k, '[');
    if (!k) {
      awarning(errtxt[CANTHANDLEPERSONCOMMENT], 4);
      no_comment++;
      return;
    }
    k++;
  }
  e = strchr(k, ']');
  u->people += atoi(k);
  j = strchr(k, ',');
  if (!j)
    j = strchr(k, ';');
  if (!j || j > e) {
    awarning(errtxt[CANTHANDLESILVERCOMMENT], 4);
    no_comment++;
    return;
  }
  while (!isdigit(*j))
    j++;
  u->money += atoi(j);
  while (isdigit(*j))
    j++;                        /* hinter die Zahl */

  k = j;
  j = strchr(k, ',');
  if (!j)
    j = strchr(k, ';');

  if (j) {
    j++;
    if (j < e && *j == 'U') {   /* Muß ein Gebäude unterhalten */
      j++;
      if (isdigit(*j)) {
        u->unterhalt = atoi(j);
        while (isdigit(*j))
          j++;
        j++;
      }
    }

    if (j < e && *j == 'I') {   /* I wie Illusion, auch Untote - eben  alles, was nix frißt und keinen langen   Befehl braucht */
      u->lives = -1;
      j += 2;                   /* hinter das I und das Zeichen danach */
    }
    if (j < e && *j == 's') {   /* Ist auf einem Schiff */
      j++;
      if (u->ship >= 0)         /* hat sonst schon ein Schiffskommando! */
        u->ship = btoi(j);
    } else if (j < e && *j == 'S') {    /* Ist Kapitän auf einem Schiff */
      j++;
      s = -btoi(j);
      for (t = units; t; t = t->next)   /* vielleicht hat schon * eine  Einheit durch */
        if (t->ship == s) {     /* BETRETE das Kommando; das ist ja falsch  */
          t->ship = -s;
          break;
        }
      u->ship = s;
    }
  }
  addregion(Rx, Ry, u->people);
}

void orders_for_temp_unit(unit * u)
{
  if (u->start_of_orders_line && u->region == Regionen) {
    /*
     * in Regionen steht die aktuelle Region drin
     */
    sprintf(warn_buf, errtxt[ALREADYUSEDINLINE],
      itob(u->no), u->start_of_orders_line);
    anerror(warn_buf);
    /*
     * return; Trotzdem kein return, damit die Befehle ordnungsgemäß
     * zugeordnet werden!
     */
  }
  u->line_no = line_no;
  u->lives = 1;
  if (u->order) {
    free(u->order);
    u->order = 0;
  }
  u->order = strdup(order_buf);
  if (u->start_of_orders) {
    free(u->start_of_orders);
    u->start_of_orders = 0;
  }
  u->start_of_orders = strdup(order_buf);
  u->start_of_orders_line = line_no;
  mother_unit = order_unit;
  set_order_unit(u);
}

void long_order(void)
{
  char *s, *q;
  int i;

  if (order_unit->long_order_line) {
    s = order_unit->long_order;
    q = strchr(s, ' ');
    if (q)
      *q = 0;                   /* Den Befehl extrahieren */
    i = findkeyword(s);
    switch (i) {
    case K_CAST:
      if (this_command == i)
        return;
      /*
       * ZAUBERE ist zwar kein langer Befehl, aber man darf auch
       * keine anderen langen haben - darum ist bei denen
       * long_order()
       */
    case K_SELL:
    case K_BUY:
      if (this_command == K_SELL || this_command == K_BUY)
        return;
      /*
       * Es sind mehrere VERKAUFE und KAUFE pro Einheit möglich
       */
    }
    if ((i == K_FOLLOW && this_command != K_FOLLOW) ||
      (i != K_FOLLOW && this_command == K_FOLLOW))
      return;                   /* FOLGE ist nur Trigger */

    if (strlen(order_unit->long_order) > DESCRIBESIZE + NAMESIZE)
      order_unit->long_order[DESCRIBESIZE + NAMESIZE] = 0;
    /*
     * zu lange Befehle kappen
     */
    sprintf(warn_buf, errtxt[UNITALREADYHASLONGORDERS],
      uid(order_unit), order_unit->long_order_line, order_unit->long_order);
    awarning(warn_buf, 1);
  } else {
    order_unit->long_order = strdup(order_buf);
    order_unit->long_order_line = line_no;
  }
}

void checknaming(void)
{
  int i;
  char *s;

  scat(printkeyword(K_NAME));
  i = findparam(getstr());
  s = getstr();
  if (i == P_FOREIGN) {
    Scat(printparam(P_FOREIGN));
    i = findparam(s);
    s = getstr();
  }
  if (strchr(s, '('))
    awarning(errtxt[NAMECONTAINSBRACKETS], 1);

  switch (i) {
  case -1:
    anerror(errtxt[UNRECOGNIZEDOBJECT]);
    break;

  case P_ALLIANCE:
  case P_UNIT:
  case P_FACTION:
  case P_BUILDING:
  case P_CASTLE:
  case P_SHIP:
  case P_REGION:
    Scat(printparam(i));
    checkstring(s, NAMESIZE, NECESSARY);
    break;

  default:
    anerror(errtxt[CANTRENAMEOBJECT]);
  }
}

void checkdisplay(void)
{
  int i;

  scat(printkeyword(K_DESCRIBE));
  i = findparam(getstr());

  switch (i) {
  case -1:
    anerror(errtxt[UNRECOGNIZEDOBJECT]);
    break;

  case P_REGION:
  case P_UNIT:
  case P_BUILDING:
  case P_CASTLE:
  case P_SHIP:
  case P_PRIVAT:
    Scat(printparam(i));
    checkstring(getstr(), 0, POSSIBLE);
    break;

  default:
    anerror(errtxt[CANTDESCRIBEOBJECT]);
  }
}

void check_leave(void)
{
  unit *t, *T = NULL;
  int s;

  s = order_unit->ship;
  order_unit->ship = 0;
  for (t = units; t; t = t->next)
    if (t->region == order_unit->region) {
      t->unterhalt += order_unit->unterhalt;
      strcpy(message_buf, uid(order_unit));
      sprintf(warn_buf, errtxt[MAINTAINANCEMOVED], message_buf, uid(t));
      break;
    }
  order_unit->unterhalt = 0;    /* ACHTUNG! hierdurch geht die  Unterhaltsinfo verloren! */
  if (s < 0) {                  /* wir waren Kapitän, neuen suchen */
    for (t = units; t; t = t->next)
      if (t->ship == -s)        /* eine Unit auf dem selben Schiff */
        T = t;                  /* in ECheck sind die Units down->top,  darum */
    if (T)                      /* die letzte der Liste==erste Unit im *  Programm */
      T->ship = s;
  }
}

void checkenter(void)
{
  int i, n;
  unit *u;

  scat(printkeyword(K_ENTER));
  i = findparam(getstr());

  switch (i) {
  case -1:
    anerror(errtxt[UNRECOGNIZEDOBJECT]);
    return;
  case P_BUILDING:
  case P_CASTLE:
  case P_SHIP:
    Scat(printparam(i));
    n = getI();
    if (!n) {
      anerror(errtxt[OBJECTNUMBERMISSING]);
      return;
    } else
      bcat(n);
    break;
  default:
    anerror(errtxt[CANTENTEROBJECT]);
    return;
  }
  check_leave();
  if (i == P_SHIP) {
    order_unit->ship = n;
    for (u = units; u; u = u->next)     /* ggf. Kommando geben */
      if (u->ship == -n)        /* da hat einer schon das Kommando */
        return;
    order_unit->ship = -n;
  }
}

int getaspell(char *s, char spell_typ, unit * u, int reallycast)
{
  t_spell *sp;
  int p;

  if (*s == '[' || *s == '<') {
    anerror(errtxt[ERRORSPELLSYNTAX]);
    return 0;
  }
  if (findparam(s) == P_REGION) {
    scat(printparam(P_REGION));
    s = getstr();
    if (*s) {
      p = atoi(s);
      icat(p);
      s = getstr();
      if (*s) {
        p = atoi(s);
        icat(p);
      } else {
        anerror(errtxt[ERRORCOORDINATES]);
        return 0;
      }
    } else {
      anerror(errtxt[ERRORREGIONPARAMETER]);
      return 0;
    }
    s = getstr();
  }
  if (findparam(s) == P_LEVEL) {
    scat(printparam(P_LEVEL));
    s = getstr();
    if (!*s || atoi(s) < 1) {
      anerror(errtxt[ERRORLEVELPARAMETERS]);
      return 0;
    }
    p = atoi(s);
    icat(p);
    s = getstr();
  }
  sp = findspell(s);
  if (!sp) {
    if (u) {                    /* sonst ist das der Test von GIB */
      if (show_warnings > 0)    /* nicht bei -w0 */
        anerror(errtxt[UNRECOGNIZEDSPELL]);
      if (*s >= '0' && *s <= '9')
        anerror(errtxt[MISSINGPARAMETERS]);
      qcat(s);
    }
    return 0;
  }
  qcat(sp->name);
  if (!(sp->typ & spell_typ)) {
    sprintf(warn_buf, errtxt[ISCOMBATSPELL], sp->name,
      (sp->typ & SP_ZAUBER) ? errtxt[ISNOTCOMBATSPELL] : "");
    if (show_warnings > 0)      /* nicht bei -w0 */
      anerror(warn_buf);
  } else {
    if (u && (sp->typ & SP_BATTLE) && (u->spell & sp->typ)) {
      sprintf(warn_buf, errtxt[UNITALREADYHAS], uid(u));
      switch (sp->typ) {
      case SP_POST:
        strcat(warn_buf, errtxt[POST]);
        break;
      case SP_PRAE:
        strcat(warn_buf, errtxt[PRE]);
        break;
      }
      strcat(warn_buf, errtxt[COMBATSPELLSET]);
      if (show_warnings > 0)    /* nicht bei -w0 */
        awarning(warn_buf, 1);
    }
    if (u) {
      p = sp->typ * reallycast;
      u->spell |= p;
      u->money -= sp->kosten;
      u->reserviert -= sp->kosten;
    }
  }
  do {
    s = getstr();
    if (*s)
      Scat(s);
  }
  while (*s);                   /* restliche Parameter ohne Check ausgeben  */
  return 1;
}

void checkgiving(void)
{
  char *s;
  int i, n;

  if (from_temp_unit_no) {
    anerror(errtxt[TEMPUNITSCANTGIVE]);
    return;
  }
  scat(printkeyword(K_GIVE));
  getaunit(NECESSARY);
  s = getstr();
  if (!getaspell(s, SP_ALL, NULL, 0)
    && !isparam(s, P_CONTROL, 1)
    && !isparam(s, P_HERBS, 1) && !isparam(s, P_UNIT, 1)) {
    n = atoi(s);
    if (n < 1) {
      if (findparam(s) == P_ALLES) {    /* GIB xx ALLES wasauchimmer */
        n = -1;
        Scat(printparam(P_ALLES));
      } else if (findparam(s) == P_EACH) {
        Scat(printparam(P_EACH));
        s = getstr();
        n = atoi(s);
        if (order_unit->people) {
          n *= order_unit->people;
        }
        if (n < 1) {
          anerror(errtxt[NUMMISSING]);
          n = 1;
        }
      } else {
        anerror(errtxt[NUMMISSING]);
        n = 1;
      }
    }
    if (n > 0)
      icat(n);

    s = getstr();
    if (!(*s) && n < 0) {       /* GIB xx ALLES */
      if (cmd_unit) {
        n = order_unit->money - order_unit->reserviert;
        cmd_unit->money += n;
        cmd_unit->reserviert += n;
      }
      order_unit->money = order_unit->reserviert;
      return;
    }

    if (!(*s)) {
      anerror(errtxt[GIVEWHAT]);
      return;
    }
    i = findparam(s);
    switch (i) {
    case P_PERSON:
      Scat(printparam(i));
      if (n < 0)
        n = order_unit->people;
      if (cmd_unit)
        cmd_unit->people += n;
      order_unit->people -= n;
      if (order_unit->people < 0 && no_comment < 1 && !does_default) {
        sprintf(warn_buf, errtxt[UNITMISSPERSON], uid(order_unit));
        awarning(warn_buf, 4);
      }
      break;

    case P_SILVER:
      Scat(printparam(i));
      if (n < 0)
        n = order_unit->money - order_unit->reserviert;
      if (cmd_unit) {
        cmd_unit->money += n;
        cmd_unit->reserviert += n;
      }
      order_unit->money -= n;
      if (order_unit->money < 0 && no_comment < 1 && !does_default) {
        sprintf(warn_buf, errtxt[UNITMISSSILVER], uid(order_unit));
        awarning(warn_buf, 4);
      }
      break;

    default:
      i = finditem(s);
      if (i < 0) {
        i = findherb(s);
        if (i < 0) {
          i = findpotion(s);
          if (i >= 0) {
            if (piping) {
              strcpy(warn_buf, printliste(i, potionnames));
              s = strchr(warn_buf, ' ');
              if (s)
                *s = 0;
              Scat(warn_buf);
            } else {
              qcat(printliste(i, potionnames));
            }
          } else {
            awarning(errtxt[UNRECOGNIZEDOBJECT], 1);
          }
        } else {
          if (piping) {
            strcpy(warn_buf, printliste(i, herbdata));
            s = strchr(warn_buf, ' ');
            if (s)
              *s = 0;
            Scat(warn_buf);
          } else {
            qcat(printliste(i, herbdata));
          }
        }
      } else {
        if (piping) {
          strcpy(warn_buf, ItemName(i, n != 1));
          s = strchr(warn_buf, ' ');
          if (s)
            *s = 0;
          Scat(warn_buf);
        } else {
          qcat(ItemName(i, n != 1));
        }
      }
      break;
    }
  } else if (findparam(s) == P_CONTROL) {
    if (order_unit->ship && !does_default) {
      if (order_unit->ship > 0) {
        sprintf(warn_buf, errtxt[UNITMISSCONTROL], uid(order_unit),
          itob(order_unit->ship));
        awarning(warn_buf, 4);
      } else if (cmd_unit) {
        if (cmd_unit->ship != 0 && abs(cmd_unit->ship) != -order_unit->ship) {
          sprintf(warn_buf, errtxt[UNITNOTONSHIPBUTONSHIP],
            uid(cmd_unit), itob(-order_unit->ship), itob(abs(cmd_unit->ship)));
          awarning(warn_buf, 4);
        }
        cmd_unit->ship = order_unit->ship;
      }
      order_unit->ship = -order_unit->ship;
    } else if (order_unit->unterhalt) {
      if (cmd_unit)
        cmd_unit->unterhalt = order_unit->unterhalt;
      order_unit->unterhalt = 0;
    }
  }
}

void getluxuries(int cmd)
{
  char *s;
  int n, i;

  s = getstr();

  n = atoi(s);
  if (n < 1) {
    if (findparam(s) == P_ALLES) {
      if (cmd == K_BUY) {
        anerror(errtxt[BUYALLNOTPOSSIBLE]);
        return;
      } else
        Scat(printparam(P_ALLES));
    } else {
      anerror(errtxt[NUMLUXURIESMISSING]);
    }
    n = 1;
  }

  icat(n);
  s = getstr();
  i = finditem(s);

  if (ItemPrice(i) < 1)
    anerror(errtxt[NOLUXURY]);
  else {
    Scat(ItemName(i, n != 1));
    if (cmd == K_BUY) {         /* Silber abziehen; nur Grundpreis! */
      i = ItemPrice(i);
      order_unit->money -= i * n;
      order_unit->reserviert -= i * n;
    }
  }
}

void checkmake(void)
{
  int i, j = 0, k = 0;
  char *s;
  char bf[200];

  scat(printkeyword(K_MAKE));
  s = getstr();

  if (isdigit(*s)) {            /* MACHE anzahl "Gegenstand" */
    j = atoi(s);
    if (j == 0)
      awarning(errtxt[NUMBER0SENSELESS], 2);
    s = getstr();
  }

  i = findparam(s);

  switch (i) {
  case P_HERBS:
    if (j)
      icat(j);
    Scat(printparam(P_HERBS));
    long_order();
    return;

  case P_TEMP:
    if (j)
      anerror(errtxt[NUMBERNOTPOSSIBLE]);
    next_indent = INDENT_NEW_ORDERS;
    scat(" TEMP");
    j = getb();
    if (!j)
      anerror(errtxt[NOTEMPNUMBER]);
    else {
      unit *u;

      bcat(j);
      s = getstr();
      if (*s)
        qcat(s);
      from_temp_unit_no = j;
      u = newunit(j, 42);
      if (u->ship == 0)
        u->ship = abs(order_unit->ship);
      orders_for_temp_unit(u);
    }
    return;

  case P_SHIP:
    if (j > 0)
      icat(j);
    k = getI();
    Scat(printparam(i));
    if (k)
      bcat(k);
    long_order();
    return;

  case P_ROAD:
    if (j > 0)
      icat(j);
    Scat(printparam(i));
    k = getdirection();
    if (k < 0) {
      sprintf(bf, "%s %s %s", printkeyword(K_MAKE),
        printparam(P_ROAD), errtxt[DIRECTION]);
      anerror(bf);
    } else
      Scat(printdirection(k));
    long_order();
    return;
  }

  i = findshiptype(s);
  if (i != -1) {
    qcat(printliste(i, shiptypes));
    long_order();
    getI();
    return;
  }

  i = finditem(s);
  if (i >= 0 && ItemPrice(i) == 0) {
    if (j)
      icat(j);
    if (piping) {
      strcpy(warn_buf, ItemName(i, 1));
      s = strchr(warn_buf, ' ');
      if (s)
        *s = 0;
      Scat(warn_buf);
    } else {
      qcat(ItemName(i, 1));
    }
    long_order();
    return;
  }

  i = findpotion(s);
  if (i != -1) {
    if (j)
      icat(j);
    if (piping) {
      strcpy(warn_buf, printliste(i, potionnames));
      s = strchr(warn_buf, ' ');
      if (s)
        *s = 0;
      Scat(warn_buf);
    } else {
      qcat(printliste(i, potionnames));
    }
    long_order();
    return;
  }

  i = findbuildingtype(s);
  if (i != -1) {
    if (j)
      icat(j);
    qcat(printliste(i, buildingtypes));
    long_order();
    k = getI();
    if (k)
      bcat(k);
    return;
  }

  /*
   * Man kann MACHE ohne Parameter verwenden, wenn man in einer Burg
   * oder einem Schiff ist. Ist aber trotzdem eine Warnung wert.
   */
  if (s[0])
    anerror(errtxt[CANTMAKETHAT]);
  else
    awarning(errtxt[UNITMUSTBEONSHIP], 4);
  long_order();
  /*
   * es kam ja eine Meldung - evtl. kennt ECheck das nur nicht?
   */
}

void checkdirections(int key)
{
  int i, count = 0, x, y, sx, sy, rx, ry;

  rx = sx = x = Rx;
  ry = sy = y = Ry;
  scat(printkeyword(key));

  do {
    i = getdirection();
    if (i <= -1) {
      if (i == -2)
        break;                  /* Zeile zu ende */
      anerror(errtxt[UNRECOGNIZEDDIRECTION]);
    } else {
      if (key == K_ROUTE && i == D_PAUSE && count == 0)
        awarning(errtxt[ROUTESTARTSWITHPAUSE], 2);
      if (key == K_MOVE && i == D_PAUSE) {
        anerror(errtxt[MOVENOTPOSSIBLEWITHPAUSE]);
        return;
      } else {
        Scat(printdirection(i));
        count++;
        if (!noship && order_unit->ship == 0 && key != K_ROUTE && count == 4) {
          sprintf(warn_buf, errtxt[UNITMOVESTOOFAR], uid(order_unit));
          awarning(warn_buf, 4);
        }
        switch (i) {
        case D_NORTHEAST:
          y++;
          break;
        case D_NORTHWEST:
          y++;
          x--;
          break;
        case D_SOUTHEAST:
          y--;
          x++;
          break;
        case D_SOUTHWEST:
          y--;
          break;
        case D_EAST:
          x++;
          break;
        case D_WEST:
          x--;
          break;
        case D_PAUSE:
          if (rx == sx && ry == sy) {
            rx = x;
            ry = y;             /* ROUTE: ersten Abschnitt merken für  Silber verschieben */
          }
          break;
        }
      }
    }
  }
  while (i >= 0);

  if (!count)
    anerror(errtxt[UNRECOGNIZEDDIRECTION]);
  if (key == K_ROUTE && !noroute && (sx != x || sy != y)) {
    sprintf(warn_buf, errtxt[ROUTENOTCYCLIC], sx, sy, x, y);
    awarning(warn_buf, 4);
  }
  if (!does_default) {
    if (order_unit->hasmoved) {
      sprintf(warn_buf, errtxt[UNITALREADYHASMOVED], uid(order_unit));
      anerror(warn_buf);
      return;
    }
    order_unit->hasmoved = 2;   /* 2: selber bewegt */
    if (key == K_ROUTE) {
      order_unit->newx = rx;
      order_unit->newy = ry;
    } else {
      order_unit->newx = x;
      order_unit->newy = y;
    }
  }
}

void check_sabotage(void)
{
  if (getparam() != P_SHIP) {
    anerror(errtxt[ONLYSABOTAGESHIP]);
    return;
  }
  Scat(printparam(P_SHIP));
  return;
}

void checkmail(void)
{
  char *s;

  scat(printkeyword(K_MESSAGE));
  s = getstr();
  if (stricmp(s, "an") == 0 || stricmp(s, "to") == 0)
    s = getstr();

  switch (findparam(s)) {
  case P_FACTION:
    Scat(printparam(P_FACTION));
    break;

  case P_REGION:
    Scat(printparam(P_REGION));
    break;

  case P_UNIT:
    Scat(printparam(P_UNIT));
    bcat(getb());
    break;

  case P_SHIP:
    Scat(printparam(P_SHIP));
    bcat(getb());
    break;

  default:
    anerror(errtxt[MSGTO]);
    break;
  }
  s = getstr();
  checkstring(s, 0, NECESSARY);
}

void claim(void)
{
  char *s;
  int i, n;

  scat(printkeyword(K_CLAIM));
  s = getstr();
  n = atoi(s);
  if (n < 1) {
    anerror(errtxt[NUMMISSING]);
    n = 1;
  } else {
    s = getstr();
  }
  icat(n);
  if (!(*s)) {
    anerror(errtxt[UNRECOGNIZEDOBJECT]);
    return;
  }
  i = finditem(s);
  if (i <= 0)
    awarning(errtxt[UNRECOGNIZEDOBJECT], 1);
  Scat(ItemName(i, n != 1));
}

void reserve(void)
{
  char *s;
  int i, n;

  scat(printkeyword(K_RESERVE));

  if (from_temp_unit_no) {
    anerror(errtxt[TEMPUNITSCANTRESERVE]);
    return;
  }

  s = getstr();
  n = atoi(s);
  if (n < 1) {
    if (findparam(s) == P_EACH) {
      s = getstr();
      n = atoi(s);
      n *= order_unit->people;
      if (n < 1) {
        anerror(errtxt[NUMMISSING]);
        n = 1;
      }
    } else {
      anerror(errtxt[NUMMISSING]);
      n = 1;
    }
  }
  if (n > 0)
    icat(n);

  s = getstr();

  if (!(*s)) {
    anerror(errtxt[RESERVEWHAT]);
    return;
  }
  i = finditem(s);
  if (i >= 0) {
    Scat(ItemName(i, n != 1));
    return;
  }

  if (findparam(s) == P_SILVER) {
    Scat(printparam(P_SILVER));
    order_unit->region->reserviert += n;
    order_unit->reserviert += n;
    return;
  }

  i = findpotion(s);
  if (i >= 0) {
    Scat(printliste(i, potionnames));
    return;
  }

  i = findherb(s);
  if (i >= 0) {
    Scat(printliste(i, herbdata));
    return;
  }
}

void check_ally(void)
{
  int i;
  char *s;

  scat(printkeyword(K_HELP));
  getafaction(getstr());
  s = getstr();

  i = findparam(s);
  switch (i) {
  case P_GIVE:
  case P_FIGHT:
  case P_SILVER:
  case P_OBSERVE:
  case P_GUARD:
  case P_FACTIONSTEALTH:
  case P_ALLES:
  case P_NOT: /* server actually accepts HELFE foe NICHT = HELFE foe ALLES NICHT */
    Scat(printparam(i));
    break;
  default:
    anerror(errtxt[ERRORHELP]);
    return;
  }

  s = getstr();
  if (findparam(s) == P_NOT) {
    Scat(printparam(P_NOT));
  } else {
    anerror(errtxt[TOOMANYPARAMETERS]);
  }
}

int studycost(t_skills * talent)
{
  if (does_default)
    return 0;
  if (talent->kosten < 0) {
    int i;

    /*
     * Lerne Magie [gebiet] 2000 -> Lernkosten=2000 Silber
     */
    char *s = getstr();

    i = findstr(magiegebiet, s, 5);
    if (i >= 0) {
      fprintf(ERR, errtxt[SCHOOLCHOSEN], magiegebiet[i]);
      i = geti();
    } else
      i = atoi(s);
    if (i < 100) {
      i = 200;
      awarning(errtxt[ASSUMING200STUDYCOSTS], 2);
    }
    return i;
  }
  return talent->kosten;
}

void
check_destroy(void) {
  char *s;
  int n;

  scat(printkeyword(K_DESTROY));
  s = getstr();

  if (isdigit(*s)) { /* ZERSTOERE anzahl ...*/
    n = atoi(s);
    if (n == 0)
      awarning(errtxt[NUMBER0SENSELESS], 2);
    icat(n);
    s = getstr();
  }
  if (*s) {
    int i;
    i = findparam(s);
    switch (i) {
    case P_ROAD:
      Scat(printparam(i));
      i = getdirection();
      if (i < 0 || i == D_PAUSE) {
        anerror(errtxt[UNRECOGNIZEDDIRECTION]);
      } else {
        Scat(printdirection(i));
      }
      return;
    default:
      anerror(errtxt[DESTROYNOSTREET]);
      break;
    }
  }
}


void check_comment(void)
{
  char *s;
  int m;

  s = getstr();
  if (strnicmp(s, "ECHECK", 6))
    return;
  s = getstr();

  if (strnicmp(s, "VERSION", 7) == 0) {
    fprintf(ERR, "%s\n", order_buf);
    return;
  }
  if (strnicmp(s, "NOWARN", 6) == 0) {  /* Warnungen für nächste   Zeile aus */
    warn_off = 2;
    return;
  }
  if (strnicmp(s, "LOHN", 4) == 0 || strnicmp(s, "WAGE", 4) == 0) {     /* LOHN   für   Arbeit  */
    m = geti();
    lohn = (m > 10) ? m : 10;
    return;
  }
  if (strnicmp(s, "ROUT", 4) == 0) {    /* ROUTe */
    noroute = (char)(1 - noroute);
    return;
  }
  if (strnicmp(s, "KOMM", 4) == 0 || strnicmp(s, "COMM", 4) == 0) {     /* KOMMando  */
    m = geti();
    if (!m) {
      m = order_unit->ship;
      if (!m)
        m = rand();
    }
    order_unit->ship = -abs(m);
    return;
  }
  if (strnicmp(s, "EMPTY", 5) == 0) {
    order_unit->money = 0;
    order_unit->reserviert = 0;
    return;
  }
  if (strnicmp(s, "NACH", 4) == 0 || strnicmp(s, "MOVE", 4) == 0) {
    order_unit->hasmoved = 1;
    order_unit->newx = geti();
    order_unit->newy = geti();
    return;
  }
  m = atoi(s);
  if (m) {
    if (order_unit) {
      order_unit->money += m;
      order_unit->reserviert += m;
    }
    return;
  }
}

void check_money(bool do_move)
{                               /* do_move=true: vor der Bewegung,  anschließend */
  unit *u, *t;                  /* Bewegung ausführen, damit das Silber  bewegt wird */
  t_region *r;
  int i, x, y, um;

  u = find_unit(-1, 0);
  if (u) {                      /* Unit -1 leeren */
    u->people = u->money = u->unterhalt = u->reserviert = 0;
    u->lives = -1;
  }

  if (do_move) {
    for (u = units; u; u = u->next) {   /* Check TEMP und leere Einheiten */
      if (u->lives < 1)         /* fremde Einheit oder Untot/Illusion */
        continue;               /* auslassen, weil deren Geldbedarf nicht  zählt */

      if (u->temp && abs(u->temp) != 42) {
        sprintf(warn_buf, errtxt[TEMPNOTTEMP], itob(u->no));
        Error(warn_buf, u->line_no, u->order);
      }
      if (u->people < 0) {
        sprintf(warn_buf, errtxt[UNITHASPERSONS], uid(u), u->people);
        warn(warn_buf, u->line_no, 3);
      }

      if (u->people == 0 && ((!nolost && !u->temp && u->money > 0) || u->temp)) {
        if (u->temp) {
          if (u->money > 0)
            sprintf(warn_buf, errtxt[UNITHASNTPERSONS], itob(u->no));
          else
            sprintf(warn_buf, errtxt[TEMPHASNTPERSONS], itob(u->no));
          warn(warn_buf, u->line_no, 2);
        } else if (no_comment <= 0) {
          sprintf(warn_buf, errtxt[UNITLOSESITEMS], itob(u->no));
          warn(warn_buf, u->line_no, 3);
        }
      }
    }
  }

  if (Regionen && no_comment < 1) {
    if (silberpool && do_move) {
      for (u = units; u; u = u->next) {
        u->region->geld += u->money;
        /*
         * Reservierung < Silber der Unit? Silber von anderen
         * Units holen
         */
        if (u->reserviert > u->money) {
          i = u->reserviert - u->money;
          for (t = units; t && i > 0; t = t->next) {
            if (t->region != u->region || t == u)
              continue;
            um = t->money - t->reserviert;
            if (um > i) um = i;
            if (um > 0) {
              u->money += um;
              i -= um;
              t->money -= um;
            }
          }
        }
      }
    }

    if (do_move)
      for (r = Regionen; r; r = r->next) {
        if (r->reserviert > 0 && r->reserviert > r->geld) {     /* nur  explizit   mit  RESERVIERE  */
          sprintf(warn_buf, errtxt[RESERVEDTOOMUCH], r->name,
            r->x, r->y, r->reserviert, r->geld);
          warn(warn_buf, r->line_no, 3);
        }
      }

    for (u = units; u; u = u->next) {   /* fehlendes Silber aus dem  Silberpool nehmen */
      if (do_move && u->unterhalt) {
        u->money -= u->unterhalt;
        u->reserviert -= u->unterhalt;
      }
      if (u->money < 0 && silberpool) {
        for (t = units; t && u->money < 0; t = t->next) {
          if (t->region != u->region || t == u)
            continue;
          um = t->money - t->reserviert;
          if (um < -u->money) um = -u->money;
          if (um > 0) {
            u->money += um;
            u->reserviert += um;        /* das so erworbene Silber muß  auch reserviert sein */
            t->money -= um;
          }
        }
      }
      if (u->money < 0) {
        sprintf(warn_buf, errtxt[UNITHASSILVER],
          uid(u), do_move ? errtxt[BEFOREINCOME] : "", u->money);
        warn(warn_buf, u->line_no, 3);
        if (u->unterhalt) {
          if (do_move)
            sprintf(warn_buf, errtxt[BUILDINGNEEDSSILVER], uid(u), -u->money);
          else
            sprintf(warn_buf, errtxt[CANTMAINTAINBUILDING], uid(u), -u->money);
          warn(warn_buf, u->line_no, 1);
        }
      }
    }
  }

  if (Regionen)
    for (r = Regionen; r; r = r->next)
      r->geld = 0;              /* gleich wird Geld bei den Einheiten  bewegt, darum den Pool leeren und nach  der Bewegung nochmal füllen */
  if (!do_move)                 /* Ebenso wird es in check_living nochmal  neu eingezahlt */
    return;

  if (!Regionen)                /* ohne Regionen Bewegungs-Check nicht *  sinnvoll */
    return;

  for (u = units; u; u = u->next) {     /* Bewegen vormerken */
    if (u->lives < 1)           /* fremde Einheit oder Untot/Illusion */
      continue;
    if (u->hasmoved > 1) {
      if (!noship && u->ship > 0) {
        sprintf(warn_buf, errtxt[UNITMOVESSHIP], uid(u), itob(u->ship));
        warn(warn_buf, u->line_no, 4);
      }
      i = -u->ship;
      if (i > 0) {              /* wir sind Kapitän; alle Einheiten auf  dem Schiff auch bewegen */
        x = u->newx;
        y = u->newy;
        for (t = units; t; t = t->next) {
          if (t->ship == i) {
            if (t->hasmoved > 1) {      /* schon bewegt! */
              sprintf(warn_buf, errtxt[UNITONSHIPHASMOVED], uid(t), itob(i));
              warning(warn_buf, t->line_no, t->long_order, 2);
            }
            t->hasmoved = 1;
            t->newx = x;
            t->newy = y;
          }
        }
      }
    }
  }

  for (u = units; u; u = u->next) {     /* Bewegen ausführen */
    if (u->lives < 1)           /* fremde Einheit oder Untot/Illusion */
      continue;

    if (u->transport && u->drive && u->drive != u->transport) {
      sprintf(checked_buf, errtxt[RIDESWRONGUNIT], uid(u), Uid(u->transport));
      scat(Uid(u->drive));
      warning(checked_buf, u->line_no, u->long_order, 1);
      continue;
    }
    if (u->drive) {             /* FAHRE; in u->transport steht die  transportierende Einheit */
      if (u->hasmoved > 0) {
        sprintf(warn_buf, errtxt[UNITALREADYHASMOVED], uid(u));
        Error(warn_buf, u->line_no, u->long_order);
      }
      if (u->transport == 0) {
        t = find_unit(u->drive, 0);
        if (!t)
          t = find_unit(u->drive, 1);
        if (t && t->lives) {
          sprintf(warn_buf, errtxt[DOESNTCARRY], uid(u), Uid(u->drive));
          Error(warn_buf, u->line_no, u->long_order);
        } else {                /* unbekannte Einheit -> unbekanntes Ziel */
          u->hasmoved = 1;
          u->newx = -9999;
          u->newy = -9999;
        }
      } else {
        t = find_unit(u->transport, 0);
        if (!t)
          t = find_unit(u->transport, 1);
        assert(t);
        /*
         * muß es geben, hat ja schließlich u->transport gesetzt
         */
        u->hasmoved = 1;
        u->newx = t->newx;
        u->newy = t->newy;
      }
    } else if (u->transport) {
      t = find_unit(u->transport, 0);
      if (!t)
        t = find_unit(u->transport, 1);
      if (t && t->lives && t->drive != u->no) {
        sprintf(warn_buf, errtxt[DOESNTRIDE], Uid(u->transport), uid(u));
        Error(warn_buf, u->line_no, u->long_order);
      }
    }

    if (u->hasmoved) {          /* NACH, gefahren oder auf Schiff */
      addregion(u->region->x, u->region->y, -(u->people));
      u->region = addregion(u->newx, u->newy, u->people);
      if (u->region->line_no == line_no)        /* line_no => NÄCHSTER */
        u->region->line_no = u->line_no;
    }
  }
}

void check_living(void)
{
  unit *u;
  t_region *r;

  /*
   * Für die Nahrungsversorgung ist auch ohne Silberpool alles Silber
   * der Region zuständig
   */

  for (u = units; u; u = u->next) {     /* Silber der Einheiten in den  Silberpool "einzahlen" */
    if (u->lives < 1)           /* jetzt nach der Umverteilung von Silber */
      continue;
    u->region->geld += u->money;        /* jetzt wird reserviertes Silber  nicht festgehalten */
  }

  for (r = Regionen; r; r = r->next) {
    for (u = units; u; u = u->next)
      if (u->region == r && u->lives > 0)
        r->geld -= u->people * 10;
    if (r->geld < 0) {
      sprintf(warn_buf, errtxt[REGIONMISSSILVER],
        r->name, r->x, r->y, -(r->geld));
      warn(warn_buf, r->line_no, 4);
    }
  }
}

void remove_temp(void)
{
  /*
   * Markiert das TEMP-Flag von Einheiten der letzten Region -> falsche
   * TEMP-Nummern bei GIB oder so fallen auf
   */
  unit *u;

  for (u = units; u; u = u->next)
    u->temp = -u->temp;
}

void check_teachings(void)
{
  teach *t;
  unit *u;
  int n;

  for (t = teachings; t; t = t->next) {
    t->student->schueler = t->student->people;
    if (t->teacher) {
      t->teacher->lehrer = t->teacher->people * 10;
      if (t->teacher->lehrer == 0) {
        sprintf(warn_buf, errtxt[NULLPERSONSTEACH], uid(t->teacher));
        strcat(warn_buf, uid(t->student));
        strcat(warn_buf, ".");
        warn(warn_buf, t->teacher->line_no, 4);
      }
      if (t->student->schueler == 0 && t->student->lives > 0) {
        sprintf(warn_buf, errtxt[NULLPERSONSLEARN], uid(t->student));
        strcat(warn_buf, uid(t->teacher));
        strcat(warn_buf, errtxt[TEACHED]);
        warn(warn_buf, t->student->line_no, 4);
      }
    }
  }

  for (t = teachings; t; t = t->next) {
    if (t->teacher == NULL || t->student->lives < 1) {  /* lernt ohne  Lehrer bzw. */
      t->student->schueler = 0; /* keine eigene Einheit */
      continue;
    }

    if (!t->student->lernt) {
      if (t->student->temp)     /* unbekannte TEMP-Einheit, wird * eh  schon angemeckert */
        continue;
      sprintf(warn_buf, errtxt[UNITISTEACHED], uid(t->student));
      strcat(warn_buf, uid(t->teacher));        /* uid() hat ein static  char*, das im sprintf()   dann zweimal das  selbe ergibt */
      strcat(warn_buf, errtxt[BUTDOESNTLEARN]);
      warn(warn_buf, t->student->line_no, 2);
      t->student->schueler = 0;
      continue;
    }

    n = (t->teacher->lehrer < t->student->schueler) ?
      t->teacher->lehrer : t->student->schueler;
    t->teacher->lehrer -= n;
    t->student->schueler -= n;
  }

  for (u = units; u; u = u->next) {
    if (u->lives < 1)
      continue;
    if (u->lehrer > 0) {
      sprintf(warn_buf, errtxt[UNITCANSTILLTEACH], uid(u), u->lehrer);
      warn(warn_buf, u->line_no, 5);
    }
    if (u->schueler > 0) {
      sprintf(warn_buf, errtxt[UNITNEEDSTEACHERS],
        uid(u), (u->schueler + 9) / 10);
      warn(warn_buf, u->line_no, 5);
    }
  }
}

void checkanorder(char *Orders)
{
  int i, x, old_errors;
  char *s;
  t_skills *sk;
  unit *u;

  s = strchr(Orders, ';');
  if (s)
    *s = 0;                     /* Ggf. Kommentar kappen */

  if (Orders[0] == 0)
    return;                     /* Dies war eine Kommentarzeile */

  if (Orders != order_buf) {
    /*
     * strcpy für überlappende strings - auch für gleiche - ist
     * undefiniert
     */
    strcpy(order_buf, Orders);
  }

  old_errors = error_count;

  i = igetkeyword(order_buf);
  this_command = i;
  switch (i) {
  case K_NUMBER:
    scat(printkeyword(K_NUMBER));
    i = getparam();
    if (!
      (i == P_UNIT || i == P_SHIP || i == P_BUILDING || i == P_CASTLE
        || i == P_FACTION)) {
      anerror(errtxt[ORDERNUMBER]);
      break;
    }
    Scat(printparam(i));
    if (getaunit(POSSIBLE) != 1)        /* "TEMP xx" oder "0" geht nicht */
      anerror(errtxt[WRONGNUMBER]);
    break;

  case K_BANNER:
    scat(printkeyword(K_BANNER));
    checkstring(getstr(), DESCRIBESIZE, NECESSARY);
    break;

  case K_EMAIL:
    checkemail();
    break;

  case K_ORIGIN:
    scat(printkeyword(K_ORIGIN));
    s = getstr();
    if (*s) {
      x = atoi(s);
      icat(x);
      s = getstr();
      if (*s) {
        x = atoi(s);
        icat(x);
      } else
        anerror(errtxt[NEEDBOTHCOORDINATES]);
    } else
      anerror(errtxt[NEEDBOTHCOORDINATES]);
    break;

  case K_USE:
    scat(printkeyword(K_USE));
    s = getstr();

    if (isdigit(*s)) {          /* BENUTZE anzahl "Trank" */
      i = atoi(s);
      icat(i);
      if (i == 0)
        awarning(errtxt[NUMBER0SENSELESS], 2);
      s = getstr();
    }

    i = findpotion(s);
    if (i >= 0) {
      Scat(printliste(i, potionnames));
    } else {
      i = finditem(s);
      if (i >= 0) {
        Scat(ItemName(i, 1));
      } else if (!(*s)) {
        anerror(errtxt[UNRECOGNIZEDOBJECT]);
      } else {
        awarning(errtxt[UNRECOGNIZEDOBJECT], 1);
      }
    }
    /* additional parameters may be in order */
    do {
      s = getstr();
      if (*s)
        Scat(s);
    }
    while (*s);

    break;

  case K_MESSAGE:
    checkmail();
    break;

  case K_WORK:
    scat(printkeyword(K_WORK));
    long_order();
    if (!does_default)
      order_unit->money += lohn * order_unit->people;
    break;

  case K_ATTACK:
    scat(printkeyword(K_ATTACK));
    if (getaunit(NECESSARY) == 3)
      anerror(errtxt[CANTATTACKTEMP]);
    if (!attack_warning) {
      /*
       * damit längere Angriffe nicht in Warnungs-Tiraden ausarten
       */
      awarning(errtxt[LONGCOMBATNOLONGORDER], 5);
      attack_warning = 1;
    }
    if (getaunit(42) == 42) {
      strcpy(warn_buf, errtxt[ONEATTACKPERUNIT]);
      anerror(warn_buf);
    }
    break;

  case K_SIEGE:
    scat(printkeyword(K_SIEGE));
    i = getI();
    if (!i)
      anerror(errtxt[NUMCASTLEMISSING]);
    else
      bcat(i);
    long_order();
    break;

  case K_NAME:
    checknaming();
    break;

  case K_BREED:
    scat(printkeyword(K_BREED));

    s = getstr();
    x = -1;
    if (isdigit(*s)) {          /* ZÜCHTE anzahl KRÄUTER */
      x = atoi(s);
      if (x <= 0)
        awarning(errtxt[NUMBER0SENSELESS], 2);
      s = getstr();
    }

    i = findparam(s);
    if (x > 0 && i != P_HERBS)
      anerror(errtxt[NUMBERNOTPOSSIBLE]);

    if (i == P_HERBS || i == P_HORSE) {
      /* ZÜCHTE [anzahl] KRÄUTER */
      /* ZÜCHTE PFERDE */
      Scat(printparam(i));
    } else if (s && (*s)) {
      anerror(errtxt[WRONGPARAMETER]);
    } else {
      anerror(errtxt[MISSINGPARAMETERS]);
    }
    long_order();
    break;

  case K_STEAL:
    scat(printkeyword(K_STEAL));
    getaunit(NECESSARY);
    long_order();
    break;

  case K_DESCRIBE:
    checkdisplay();
    break;

  case K_GUARD:
    scat(printkeyword(K_GUARD));
    s = getstr();
    if (findparam(s) == P_NOT) {
      Scat(printparam(P_NOT));
    } else if (*s) {
      anerror(errtxt[WRONGPARAMETER]);
    }
    break;

  case K_PAY:
    scat(printkeyword(K_PAY));
    s = getstr();
    if (findparam(s) == P_NOT) {
      Scat(printparam(P_NOT));
    }
    break;

  case K_ALLIANCE: {
    int a;
    scat(printkeyword(K_ALLIANCE));
    a = getparam();
    switch (a) {
    case P_LEAVE:
    case P_NEW:
      Scat(printparam(a));
      break;
    case P_KICK:
    case P_CONTROL:
    case P_INVITE:
    case P_JOIN:
      Scat(printparam(a));
      s = getstr();
      i = btoi(s);
      if (!*s || !i) {
        if (i == P_JOIN)
          anerror(errtxt[WRONGNUMBER]);
        else
          anerror(errtxt[WRONGFACTIONNUMBER]);
      } else {
        Scat(s);
      }
      break;
    default:
      anerror(errtxt[WRONGPARAMETER]);
      break;
    }
    expect_last(-1);

    break;
  }

  case K_END:
    if (from_temp_unit_no == 0)
      awarning(errtxt[ENDWITHOUTTEMP], 2);
    scat(printkeyword(K_END));
    indent = next_indent = INDENT_ORDERS;
    end_unit_orders();
    from_temp_unit_no = current_temp_no = 0;
    if (mother_unit) {
      set_order_unit(mother_unit);
      mother_unit = NULL;
    }
    break;

  case K_RESEARCH:
    scat(printkeyword(K_RESEARCH));
    i = getparam();
    if (i == P_HERBS) {
      Scat(printparam(P_HERBS));        /* momentan nur FORSCHE KRÄUTER */
    } else
      anerror(errtxt[RESEARCHHERBSONLY]);
    long_order();
    break;

  case K_SETSTEALTH:
    scat(printkeyword(K_SETSTEALTH));
    s = getstr();
    i = findtoken(s, UT_RACE);
    if (i >= 0) {
      Scat(printliste(i, Rassen));
      break;
    }
    i = findparam(s);
    if (i == P_FACTION) {
      Scat(printparam(i));
      s = getstr();
      if (*s) {
        if (findparam(s) == P_NOT) {
          Scat(printparam(P_NOT));
        } else if (findparam(s) == P_NUMBER) {
          Scat(printparam(P_NUMBER));
          i = getb();
/** siehe http://eressea.upb.de/mantis/view.php?id=887
            if (!i)
              anerror(errtxt[WRONGFACTIONNUMBER]);
            else */
          icat(i);
        } else
          anerror(errtxt[WRONGPARAMETER]);
      }
      break;
    }
    if (isdigit(s[0])) {
      i = atoip(s);
      icat(i);
      break;
    }
    awarning(errtxt[MISSINGDISGUISEPARAMETERS], 5); /* FIXME */
    break;

  case K_GIVE:
    checkgiving();
    break;

  case K_HELP:
    check_ally();
    break;

  case K_FIGHT:
    scat(printkeyword(K_FIGHT));
    s = getstr();
    i = findparam(s);
    switch (i) {
    case P_AGGRESSIVE:
    case P_DEFENSIVE:
    case P_NOT:
    case P_REAR:
    case P_FLEE:
    case P_FRONT:
      Scat(printparam(i));
      break;
    default:
      if (*s) {
        if (findkeyword(s) == K_HELP) {
          Scat(printkeyword(K_HELP));
          s = getstr();
          if (*s) {
            if (findparam(s) == P_NOT) {
              Scat(printparam(P_NOT));
            } else
              anerror(errtxt[WRONGFIGHTSTATE]);
          }
        } else
          anerror(errtxt[WRONGFIGHTSTATE]);
        Scat(s);
      } else
        Scat(printparam(P_FRONT));
    }
    break;

  case K_COMBATMAGIC:
    scat(printkeyword(K_COMBATMAGIC));
    s = getstr();
    getaspell(s, SP_KAMPF | SP_POST | SP_PRAE, order_unit, 1);
    break;

  case K_BUY:
  case K_SELL:
    scat(printkeyword(i));
    getluxuries(i);
    long_order();
    break;

  case K_CONTACT:
    scat(printkeyword(K_CONTACT));
    s = getstr();
    switch (i = findparam(s)) {
    case P_UNIT:
	Scat(printparam(i));
        getaunit(NECESSARY);
        break;
    case P_FACTION:
	Scat(printparam(i));
        getafaction(getstr());
        break;
    default:
        anerror(errtxt[WRONGPARAMETER]);
    }

    break;

  case K_SPY:
    scat(printkeyword(K_SPY));
    i = getb();
    if (!i)
      anerror(errtxt[UNITMISSING]);
    else
      bcat(i);
    long_order();
    break;

  case K_TEACH:
    scat(printkeyword(K_TEACH));
    if (!getmoreunits(false))
      anerror(errtxt[TEACHWHO]);
    long_order();
    break;

  case K_FORGET:
    scat(printkeyword(K_FORGET));
    sk = getskill();
    if (!sk)
      anerror(errtxt[UNRECOGNIZEDSKILL]);
    else {
      Scat(sk->name);
    }
    break;

  case K_STUDY:
    scat(printkeyword(K_STUDY));
    sk = NULL;
    s = getstr();
    if (*s) {
        if (findparam(s) == P_AUTO) {
            Scat(printparam(P_AUTO));
            sk = getskill();
        }
        else {
            sk = findskill(s);
        }
    }
    if (!sk) {
      anerror(errtxt[UNRECOGNIZEDSKILL]);
    }
    else {
      Scat(sk->name);
      if (unicode_utf8_strcasecmp(sk->name, errtxt[MAGIC]) == 0)
        if (order_unit->people > 1)
          anerror(errtxt[ONEPERSONPERMAGEUNIT]);
    }
    if (sk && !does_default) {
      x = studycost(sk) * order_unit->people;
      if (x) {
        order_unit->money -= x;
        order_unit->reserviert -= x;
      }
      addteach(NULL, order_unit);
      order_unit->lernt = 1;
    }
    long_order();
    break;

  case K_MAKE:
    checkmake();
    break;

  case K_SABOTAGE:
    scat(printkeyword(K_SABOTAGE));
    check_sabotage();
    long_order();
    break;

  case K_PASSWORD:
    scat(printkeyword(K_PASSWORD));
    s = getstr();
    if (!s[0])
      awarning(errtxt[PASSWORDCLEARED], 0);
    else
      checkstring(s, NAMESIZE, POSSIBLE);
    break;

  case K_RECRUIT:
    scat(printkeyword(K_RECRUIT));
    i = geti();
    if (i) {
      icat(i);
      if (from_temp_unit_no)
        u = newunit(from_temp_unit_no, 1);
      else
        u = order_unit;
      if (does_default)
        break;
      u->money -= i * rec_cost;
      u->reserviert -= i * rec_cost;
      u->people += i;
      addregion(Rx, Ry, i);
    } else
      anerror(errtxt[MISSINGNUMRECRUITS]);
    break;

  case K_QUIT:
    scat(printkeyword(K_QUIT));
    s = getstr();
    if (!s[0])
      anerror(errtxt[MISSINGPASSWORD]);
    else
      checkstring(s, NAMESIZE, POSSIBLE);
    awarning(errtxt[QUITMSG], 0);
    break;

  case K_TAX:
    scat(printkeyword(K_TAX));
    i = geti();
    if (i)
      icat(i);
    else
      i = 20 * order_unit->people;
    long_order();
    if (!does_default)
      order_unit->money += i;
    break;

  case K_ENTERTAIN:
    scat(printkeyword(K_ENTERTAIN));
    s = getstr();
    if (*s) {
      Scat(s);
      i = atoi(s);
      if (i <= 0)
        awarning(errtxt[NUMBER0SENSELESS], 1);
    } else {
      if (!i)
        i = 20 * order_unit->people;
    }

    if (!does_default) {
      order_unit->money += i;
    }
    long_order();
    break;

  case K_ENTER:
    checkenter();
    break;

  case K_LEAVE:
    scat(printkeyword(K_LEAVE));
    check_leave();
    break;

  case K_ROUTE:
  case K_MOVE:
    checkdirections(i);
    long_order();
    break;

  case K_FOLLOW:
    scat(printkeyword(K_FOLLOW));
    s = getstr();
    if (s) {
      i = findparam(s);
      if (i == P_UNIT) {
        Scat(printparam(i));
        getaunit(NECESSARY);
      } else if (i == P_SHIP) {
        Scat(printparam(i));
        s = getstr();
        x = btoi(s);
        Scat(s);
        long_order();
      } else
        anerror(errtxt[FOLLOW]);
    }
    break;

  case K_REPORT:
    scat(printkeyword(K_REPORT));
    s = getstr();
    i = findreport(s);
    if (i == -1) {
      if (unicode_utf8_strncasecmp(s, printkeyword(K_SHOW), strlen(s)))
        anerror(errtxt[UNRECOGNIZEDREPORTOPTION]);
      else {
        Scat(printkeyword(K_SHOW));
      }
      break;
    }
    Scat(reports[i]);
    s = getstr();
    i = findstr(message_levels, s, ML_MAX);
    if (i == -1) {
      anerror(errtxt[WRONGOUTPUTLEVEL]);
      break;
    }
    Scat(message_levels[i]);
    break;

  case K_OPTION:
    scat(printkeyword(K_OPTION));
    i = getoption();
    if (i < 0) {
      anerror(errtxt[UNRECOGNIZEDOPTION]);
      break;
    }
    Scat(printliste(i, options));
    i = getparam();
    if (i == P_WARN) {
      Scat(printparam(i));
      i = getparam();
    }
    if (i == P_NOT) {
      Scat(printparam(i));
    }
    break;

  case K_CAST:
    scat(printkeyword(K_CAST));
    s = getstr();
    getaspell(s, SP_ZAUBER, order_unit, 1);
    long_order();
    break;

  case K_SHOW:
    scat(printkeyword(K_SHOW));
    s = getstr();
    Scat(s);
    break;

  case K_DESTROY:
    check_destroy();
    long_order();
    break;

  case K_RIDE:
    scat(printkeyword(K_RIDE));
    if (getaunit(NECESSARY) == 2)
      anerror(errtxt[UNIT0NOTPOSSIBLE]);
    else if (!does_default)
      order_unit->drive = this_unit;
    long_order();
    break;

  case K_CARRY:
    scat(printkeyword(K_CARRY));
    getaunit(NECESSARY);
    if (!does_default) {
      if (cmd_unit) {
        cmd_unit->transport = order_unit->no;
        cmd_unit->hasmoved = -1;
      } else
        awarning(errtxt[NOCARRIER], 3);
    }
    if (getaunit(42) == 42)
      anerror(errtxt[ONECARRYPERUNIT]);
    break;

  case K_PIRACY:
    scat(printkeyword(K_PIRACY));
    getmoreunits(true);
    long_order();
    break;

  case K_SCHOOL:               /* Magiegebiet */
    /* not an order any more */
    s = getstr();
    i = findstr(magiegebiet, s, 5);
    if (i < 0) {
      sprintf(warn_buf, errtxt[UNRECOGNIZEDSCHOOL], s);
      anerror(warn_buf);
    } else {
      scat(printkeyword(K_SCHOOL));
      Scat(magiegebiet[i]);
    }
    break;

  case K_DEFAULT:
    scat(printkeyword(K_DEFAULT));
    scat(" \"");
    u = newunit(-1, 0);
    copy_unit(order_unit, u);
    free(order_unit->start_of_orders);
    order_unit->start_of_orders = 0;
    free(order_unit->long_order);
    order_unit->long_order = 0;
    free(order_unit->order);
    order_unit->order = 0;
    order_unit->long_order_line = 0;
    order_unit->start_of_orders_line = 0;
    order_unit->temp = 0;
    /*
     * der DEFAULT gilt ja erst nächste Runde!
     */
    s = getstr();
    does_default = 1;
    porder();
    checkanorder(s);
    does_default = 2;
    while (getstr()[0]) ;
    copy_unit(u, order_unit);
    u->no = -1;
    u->long_order_line = 0;
    u->start_of_orders_line = 0;
    u->temp = 0;
    free (u->order);
    u->order = 0;
    free (u->long_order);
    u->long_order = 0;
    free (u->start_of_orders);
    u->start_of_orders = 0;
    break;

  case K_COMMENT:
    check_comment();
    scat(Orders);
    do {
      s = getstr();
    } while (*s);
    break;

  case K_RESERVE:
    reserve();
    break;

  case K_CLAIM:
    claim();
    break;

  case K_RESTART:
    scat(printkeyword(K_RESTART));
    i = findtoken(getstr(), UT_RACE);
    if (i < 0) {
      anerror(errtxt[UNRECOGNIZEDRACE]);
      break;
    } else
      Scat(printliste(i, Rassen));
    s = getstr();
    if (!*s) {
      anerror(errtxt[MISSINGPASSWORD]);
      break;
    } else
      qcat(s);
    awarning(errtxt[RESTARTMSG], 0);
    break;

  case K_GROUP:
    scat(printkeyword(K_GROUP));
    s = getstr();
    if (*s)
      Scat(s);
    break;

  case K_SORT:
    scat(printkeyword(K_SORT));
    s = getstr();
    if (*s) {
      if (unicode_utf8_strncasecmp(s, printparam(P_BEFORE), strlen(s)) == 0 ||
        unicode_utf8_strncasecmp(s, printparam(P_AFTER), strlen(s)) == 0) {
        Scat(s);
        i = getaunit(NECESSARY);
        if (i == 1 || i == 3)   /* normale oder TEMP-Einheit: ok */
          break;
      }
    }
    anerror(errtxt[SORT]);
    break;

  case K_PLANT: {
    int n = 1;
    scat(printkeyword(K_PLANT));
    s = getstr();
    if (isdigit(*s)) {          /* PFLANZE anzahl "Kraeuter/Samen/Mallornsamen" */
      n = atoi(s);
      if (n == 0)
        awarning(errtxt[NUMBER0SENSELESS], 2);
      icat(n);
      s = getstr();
    }

    i = findparam(s);
    if (i == P_HERBS || i == P_TREES) {
      Scat(printparam(i));
    } else {
      i = finditem(s);
      if (i >= 0) {
        Scat(ItemName(i, n != 1));
      } else if (!(*s)) {
        anerror(errtxt[UNRECOGNIZEDOBJECT]);
      } else {
        awarning(errtxt[UNRECOGNIZEDOBJECT], 1);
      }
    }
    long_order();
  }
    break;

  case K_PREFIX:
    scat(printkeyword(i));
    s = getstr();
    if (*s)
      Scat(s);
    break;
  case K_PROMOTE:
  case K_PROMOTION:
    scat(printkeyword(i));
    break;
  case K_LANGUAGE:
    scat(printkeyword(i));
    s = getstr();
    if (*s)
      Scat(s);
    else
      anerror(errtxt[MISSINGPARAMETERS]);
    break;
  default:
    anerror(errtxt[UNRECOGNIZEDORDER]);
  }

  if (error_count == old_errors)
    expect_last(1);

  if (does_default != 1) {
    porder();
    does_default = 0;
  }
}

void readaunit(void)
{
  int i;
  unit *u;

  i = getb();
  if (i == 0) {
    anerror(errtxt[MISSINGUNITNUMBER]);
    get_order();
    return;
  }
  u = newunit(i, 0);
  u->line_no = line_no;
  /*
   * Sonst ist die Zeilennummer die Zeile, wo die Einheit zuerst von
   * einer anderen Einheit angesprochen wurde...
   */
  orders_for_unit(i, u);

  indent = INDENT_UNIT;
  next_indent = INDENT_ORDERS;
  porder();
  from_temp_unit_no = 0;

  for (;;) {
    get_order();

    if (befehle_ende)
      return;

    /*
     * Erst wenn wir sicher sind, daß kein Befehl eingegeben wurde,
     * checken wir, ob nun eine neue Einheit oder ein neuer Spieler
     * drankommt
     */

    i = igetkeyword(order_buf);
    switch (i) {
    case K_UNIT:
    case K_ORDERSTART:
    case K_NEXT:
    case K_REGION:
      if (from_temp_unit_no != 0) {
        sprintf(warn_buf, errtxt[MISSINGEND], itob(from_temp_unit_no));
        awarning(warn_buf, 2);
        from_temp_unit_no = 0;
      }
      return;
    default:
      if (i < -1)
        continue;                 /* Fehler: "@ Befehl" statt "@Befehl" */
      if (i < 0) {
        if (order_buf[0] == ';') {
          check_comment();
          continue;
        }
      }
      break;
    }

    if (order_buf[0])
      checkanorder(order_buf);
  }
}

int readafaction(void)
{
  int i;
  char *s;

  if (line_start == 1)
    line_no = 1;
  i = getI();
  if (i > 0) {
    bcat(i);
    s = getstr();
    if (s[0] == 0 || strcmp(s, "hier_passwort_eintragen") == 0) {
      if (compile)              /* nicht übertreiben im Compiler-Mode */
        anerror(errtxt[PASSWORDMSG1]);
      else
        fputs(errtxt[PASSWORDMSG2], ERR);
      qcat(errtxt[PASSWORDMSG3]);
    } else
      qcat(s);
  } else
    anerror(errtxt[MISSINGFACTIONNUMBER]);

  indent = next_indent = INDENT_FACTION;
  porder();
  return i;
}

void help_keys(char key)
{
  int i, j;

  switch (key) {
  case 'b':                    /* Befehle */
  case 'c':                    /* Commands */
    fprintf(ERR, "Befehle / commands:\n\n");
    for (i = 1; i < MAXKEYWORDS; i++)
      fprintf(ERR, "%s\n", Keywords[i]);
    break;

  case 's':                    /* Schlüsselworte */
  case 'k':                    /* Keywords */
    fprintf(ERR, "Schlüsselworte / keywords:\n\n");
    for (i = 1; i < UT_MAX; i++)
      fprintf(ERR, "%s\n", Keys[i]);
    break;

  case 'p':                    /* Parameter */
    fprintf(ERR, "Parameter / parameters:\n\n");
    for (i = 0; i < MAXPARAMS; i++)
      fprintf(ERR, "%s\n", Params[i]);
    break;

  case 'r':                    /* Richtungen */
  case 'd':                    /* Directions */
    fprintf(ERR, "Richtungen / directions:\n\n");
    for (i = 0; i < MAXDIRECTIONS; i++)
      fprintf(ERR, "%s\n", Directions[i]);
    break;

  case 'm':
    fprintf(ERR, "Meldungen / messages:\n\n");
    for (i = 0; i < MAX_ERRORS; i++)
      fprintf(ERR, "%s\n", Errors[i]);
    break;

  case 'f':
    fprintf(ERR, "Dateien / files:\n\n");
    for (j = UT_NONE + 1; j < UT_MAX; j++) {
      fprintf(ERR, "%s:", Keys[j]);
      for (i = 0; i < filecount; i++)
        if (ECheck_Files[i].type == j)
          fprintf(ERR, "  %s", ECheck_Files[i].name);
      putc('\n', ERR);
    }
    putc('\n', ERR);
    break;

  default:
    return;
  }
  exit(0);
}

void recurseprinthelp(t_liste * h)
{
  if (h->next)
    recurseprinthelp(h->next);
  fprintf(ERR, "%s\n", h->name);
}

void printhelp(int argc, char *argv[], int index)
{
  if (!help_caption)
    readfiles(0);               /* evtl. ist anderes echeck_locale  gesetzt; darum _jetzt_ lesen */

  if (!help_caption) {
    fprintf(ERR, "\n  **  ECheck V%s, %s  **\n\n"
      " kann keine Datei lesen!  -  can't read any file!\n\n Pfad / Path: '%s/%s'\n\n",
      echeck_version, __DATE__, path, echeck_locale);
    help_keys('f');
  }

  fprintf(ERR, help_caption, echeck_version, __DATE__, argv[0]);
  if (index > 0 && argv[index][1] == 'h') {
    if (argv[index][2] != 0)
      help_keys(argv[index][2]);
    if (argc > index + 1)
      help_keys(*argv[index + 1]);
  }

  fprintf(ERR, help_path, echeck_locale);
  putc('\n', ERR);
  recurseprinthelp(help);
  exit(1);
}

int check_options(int argc, char *argv[], char dostop, char command_line)
{
  int i;
  char *x;
  FILE *F;

  for (i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      switch (argv[i][1]) {
      case 'P':
        if (dostop) {           /* bei Optionen via "; ECHECK" nicht mehr  machen */
          if (argv[i][2] == 0) {        /* -P path */
            i++;
            if (argv[i]) {
              free(path);
              path = strdup(argv[i]);
            }
            else {
              fputs
                ("Leere Pfad-Angabe ungültig\nEmpty path invalid\n", stderr);
              exit(1);
            }
          } else if (*(argv[i] + 2)) {
            /* -Ppath */
            free(path);
            path = strdup((char *)(argv[i] + 2));
          }
        }
        break;

      case 'Q':
        verbose = 0;
        break;

      case 'C':
        compact = 1;
        break;

      case 'v':
        if (argv[i][2] == 0) {  /* -V version */
          i++;
          if (!argv[i])
            break;
        }
        x = strchr(argv[i], '.');
        if (x) {
          *x = 0;
          if (strncmp(echeck_version, argv[i] + 2, strlen(argv[i] + 2)) == 0) {
            *x = '.';
            x++;
          }
        }
        break;

      case 'b':
        brief = 1;
        break;

      case 'l':
        silberpool = 1;
        break;

      case 'q':
        no_comment = 1;
        noship = 1;
        nolost = 1;
        noroute = 1;
        break;

      case 'r':
        if (argv[i][2] == 0) {  /* -r nnn */
          i++;
          if (argv[i])
            rec_cost = atoi(argv[i]);
          else if (verbose)
            fprintf(stderr,
              "Fehlende Rekrutierungskosten, auf %d gesetzt\n"
              "Missing recruiting costs, set to %d", rec_cost, rec_cost);
        } else
          rec_cost = atoi(argv[i] + 2);
        break;

      case 'c':
        compile = OUT_COMPILE;
        break;

      case 'm':
        compile = OUT_MAGELLAN;
        break;

      case 'T':
        run_tests = argv[i][2] == '=' ? argv[i] + 3 : "all";
        break;

      case 'E':
        if (dostop) {           /* bei Optionen via "; ECHECK" nicht mehr  machen */
          echo_it = 1;
          set_output(stdout, stdout);
        }
        break;

      case 'e':
        if (dostop) {           /* bei Optionen via "; ECHECK" nicht mehr  machen */
          echo_it = 1;
          set_output(stdout, stderr);
        }
        break;

      case 'O':
        if (dostop) {           /* bei Optionen via "; ECHECK" nicht mehr  machen */
          if (argv[i][2] == 0) {        /* "-O file" */
            i++;
            x = argv[i];
          } else                /* "-Ofile" */
            x = argv[i] + 2;
          if (!x) {
            fputs
              ("Keine Datei für Fehler-Texte, stderr benutzt\n"
              "Using stderr for error output\n", stderr);
            set_output(NULL, stderr);
            break;
          }
          F = fopen(x, "w");
          if (!F) {
            fprintf(stderr,
              "Kann Datei `%s' nicht schreiben:\n"
              "Can't write to file `%s':\n" " %s", x, x, strerror(errno));
            exit(0);
          }
          set_output(NULL, F);
        }

        break;

      case 'o':
        if (dostop) {           /* bei Optionen via "; ECHECK" nicht mehr  machen */
          if (argv[i][2] == 0) {        /* "-o file" */
            i++;
            x = argv[i];
          } else                /* "-ofile" */
            x = argv[i] + 2;
          echo_it = 1;
          if (!x) {
            fputs
              ("Leere Datei für 'geprüfte Datei', stdout benutzt\n"
              "Empty file for checked file, using stdout\n", stderr);
            set_output(stdout, NULL);
            break;
          }
          F = fopen(x, "w");
          if (!F) {
            fprintf(stderr,
              "Kann Datei `%s' nicht schreiben:\n"
              "Can't write to file `%s':\n" " %s", x, x, strerror(errno));
            exit(0);
          }
          set_output(F, NULL);
        }
        break;

      case 'p':
        piping = 1;
        break;

      case 'x':
        line_start = 1;
        break;

      case 'w':
        if (command_line == 1 || warnings_cl == 0) {
          if (argv[i][2])
            show_warnings = (char)atoi(argv[i] + 2);
          else {
            if (argv[i + 1] && isdigit(*argv[i + 1])) {
              i++;
              show_warnings = (char)atoi(argv[i]);
            } else
              show_warnings = 0;
          }
        }
        if (command_line == 1)
          warnings_cl = 1;
        break;

      case 's':
        if (dostop)             /* bei Optionen via "; ECHECK" nicht mehr  machen */
          set_output(NULL, stderr);
        break;

      case 'n':
        if (strlen(argv[i]) > 2) {
          if (argv[i][3] == 0) {        /* -no xxx */
            i++;
            x = argv[i];
          } else
            x = argv[i] + 3;
          if (!x) {
            fputs("-no ???\n", stderr);
            break;
          }
          switch (*x) {
          case 's':
            noship = 1;
            break;
          case 'r':
            noroute = 1;
            break;
          case 'l':
            nolost = 1;
            break;
          case 'p':
            silberpool = 0;
            break;
          }
        } else
          ignore_NameMe = 1;
        break;

      case '-':
        if (strcmp(argv[i] + 2, "help") == 0) { /* '--help' */
          if (dostop)           /* bei Optionen via "; ECHECK" nicht mehr  machen */
            printhelp(argc, argv, i);
          else
            fprintf(ERR, "Option `%s' unbekannt.\n"
              "Unknow option `%s'\n", argv[i], argv[i]);
          if (dostop)           /* Nicht stoppen, wenn dies die Parameter  aus der Datei selbst sind! */
            exit(10);
        }
        break;

      case '?':
      case 'h':
        if (dostop)             /* bei Optionen via "; ECHECK" nicht mehr  machen */
          printhelp(argc, argv, i);
        break;
      case 'R':                /* -R rules */
        if (argv[i][2] == 0) {
          i++;
          if (argv[i]) {
            echeck_rules = argv[i];
          }
        } else {
          echeck_rules = argv[i] + 2;
        }
        break;
      case 'V':
        fprintf(stdout, "echeck version %s\n", echeck_version);
        exit(0);
      case 'L':
        if (argv[i][2] == 0) {  /* -L loc */
          i++;
          if (argv[i])
            echeck_locale = argv[i];
          else
            fputs("Fehlende Spracheinstellung, benutze 'de'\n"
              "Missing locale, using 'de'\n", stderr);
        } else {
          echeck_locale = argv[i] + 2;
        }
        break;

      default:
        if (argv[i][1]) {       /* sonst ist das nur '-' => stdin lesen */
          fprintf(ERR, "Option `%s' unbekannt.\n"
            "Unknown option `%s'\n", argv[i], argv[i]);
          if (dostop)           /* Nicht stoppen, wenn dies die Parameter  aus der Datei selbst sind! */
            exit(10);
        }
      }
    } else
      break;
  }
  return i;
}

void parse_options(char *p, char dostop)
{
  char *argv[10], **ap = argv, *vl, argc = 0;

  vl = strtok(p, " \t, ");
  do {
    *ap++ = vl;
    argc++;
  }
  while ((vl = strtok(NULL, " \t, ")) != NULL);
  *ap = 0;
  check_options(argc, argv, dostop, 0);
}

void check_OPTION(void)
{
  get_order();
  if (befehle_ende)
    return;
  if (strncmp(order_buf, "From ", 5) == 0) {    /* es ist eine Mail */
    do {                        /* Bis zur Leerzeile suchen -> Mailheader  zu Ende */
      fgetbuffer(order_buf, BUFSIZE, F);
    }
    while (order_buf[0] != '\n' && !feof(F));
    if (feof(F)) {
      befehle_ende = 1;
      return;
    }
    get_order();
  }
  if (befehle_ende)
    return;
  if (order_buf[0] == COMMENT_CHAR)
    do {
      if (strlen(order_buf) > 9) {
        if (strnicmp(order_buf, "; OPTION", 8) == 0 ||
          strnicmp(order_buf, "; ECHECK", 8) == 0) {
          parse_options((char *)(order_buf + 2), 0);
          /*
           * "; " überspringen; zeigt dann auf "OPTION"
           */
        } else if (strnicmp(order_buf, "; VERSION", 9) == 0)
          fprintf(ERR, "%s\n", order_buf);
      }
      get_order();
      if (befehle_ende)
        return;
    }
    while (order_buf[0] == COMMENT_CHAR);
}

static void check_start(int f, int *start_warning) {
  if (!f && *start_warning) {
    anerror(errtxt[MISSINGSTART]);
    *start_warning = 0;
  }
}

static void cleanup() {
  t_region *r;
  teach *t;
  unit *u;

  while (Regionen) {
    r = Regionen->next;
    if (Regionen->name)
      free(Regionen->name);
    free(Regionen);
    Regionen = r;
  }
  while (units) {
    u = units->next;
    free(units->start_of_orders);
    free(units->long_order);
    free(units->order);
    free(units);
    units = u;
  }
  while (teachings) {
    t = teachings->next;
    free(teachings);
    teachings = t;
  }
  teachings = NULL;
  Regionen = NULL;
  units = NULL;
  order_unit = NULL;
  cmd_unit = NULL;
  order_unit = NULL;
}

void process_order_file(int *faction_count, int *unit_count)
{
  int f = 0, next = 0, start_warning = 1;
  t_region *r;
  char *x;

  line_no = befehle_ende = 0;

  check_OPTION();

  if (befehle_ende)             /* dies war wohl eine Datei ohne Befehle */
    return;

  Rx = Ry = -10000;

  /*
   * Auffinden der ersten Partei, und danach abarbeiten bis zur letzten
   * Partei
   */

  while (!befehle_ende) {
    int i = igetkeyword(order_buf);
    switch (i) {
    case K_LOCALE:
      check_start(f, &start_warning);
      x = getstr();
      if (strcmp(x, echeck_locale) != 0) {
        sprintf(warn_buf, errtxt[LOCALEMISMATCH], echeck_locale, x);
        awarning(warn_buf, 0);
      }
      get_order();
      break;

    case K_REGION:
      check_start(f, &start_warning);
      if (Regionen)
        remove_temp();
      attack_warning = 0;
      if (echo_it) {
        fputs(order_buf, OUT);
        putc('\n', OUT);
      }
      x = getstr();
      if (*x) {
        if (!isdigit(*x) && (*x != '-'))
          /*
           * REGION ohne Koordinaten - z.B. Astralebene
           */
          Rx = Ry = -9999;
        else {
          Rx = atoi(x);
          x = strchr(order_buf, ',');
          if (!x) {
            x = strchr(order_buf, ' ');
            if (x)
              x = strchr(++x, ' ');     /* 2. Space ist Trenner? */
            else
              x = getstr();
          }
          if (x && *(++x))
            Ry = atoi(x);
          else
            Ry = -10000;
        }
      } else
        Rx = Ry = -10000;
      if (Rx < -9999 || Ry < -9999)
        awarning(errtxt[ERRORREGION], 0);
      r = addregion(Rx, Ry, 0);
      r->line_no = line_no;
      x = strchr(order_buf, ';');
      if (x) {
        x++;
        while (isspace(*x))
          x++;
        if (r->name)
          free(r->name);
        r->name = strdup(x);
        x = strchr(r->name, '\n');
        if (x)
          *x = 0;
      } else {
        if (r->name)
          free(r->name);
        r->name = strdup("");
      }
      get_order();
      break;

    case K_ORDERSTART:
      if (f && !next)
        awarning(errtxt[MISSINGNEXT], 0);
      scat(printkeyword(i));
      befehle_ende = 0;
      f = readafaction();
      if (brief <= 1) {
        if (compile)
          fprintf(ERR, "%s:faction:%s\n", filename, itob(f));
        else
          fprintf(ERR, errtxt[FOUNDORDERS], itob(f));
      }

      check_OPTION();           /* Nach PARTEI auf "; OPTION" bzw. ";  ECHECK" testen */
      if (faction_count)
        ++ * faction_count;
      if (befehle_ende)
        return;
      if (!compile) {
        if (verbose) {
          fprintf(ERR, errtxt[RECRUITCOSTSSET], rec_cost);
          fprintf(ERR, errtxt[WARNINGLEVEL], show_warnings);
          if (silberpool)
            fputs(errtxt[SILVERPOOL], ERR);
          fputs("\n\n", ERR);
        }
      }
      next = 0;
      break;

    case K_UNIT:
      check_start(f, &start_warning);
      if (f) {
        scat(order_buf);
        readaunit();
        if (unit_count)
          ++ * unit_count;
      } else
        get_order();
      break;

      /*
       * Falls in readunit abgebrochen wird, steht dort entweder
       * eine neue Partei, eine neue Einheit oder das File-Ende. Das
       * switch wird erneut durchlaufen, und die entsprechende
       * Funktion aufgerufen. Man darf order_buf auf alle Fälle
       * nicht überschreiben! Bei allen anderen Einträgen hier
       * muß order_buf erneut gefüllt werden, da die betreffende
       * Information in nur einer Zeile steht, und nun die nächste
       * gelesen werden muß.
       */

    case K_NEXT:
      check_start(f, &start_warning);
      f = 0;
      scat(printkeyword(K_NEXT));
      indent = next_indent = INDENT_FACTION;
      porder();
      next = 1;
      start_warning = 1;

      check_money(true);        /* Check für Lerngeld, Handel usw.; true:   dann Bewegung ausführen */
      if (Regionen) {
        check_money(false);     /* Silber nochmal in den Pool, fehlendes  aus Pool */
        check_living();         /* Ernährung mit allem Silber der Region */
      }
      check_teachings();
      cleanup();

      get_order();
      break;

    default:
      if (order_buf[0] == ';') {
        check_comment();
      } else {
        if (f && order_buf[0])
          awarning(errtxt[NOTEXECUTED], 1);
        else if (start_warning && order_buf[0]) {
          anerror(errtxt[MISSINGSTART]);
          start_warning = 0;
        }
      }
      get_order();
    }
  }                             /* end while !befehle_ende */

  if (igetkeyword(order_buf) == K_NEXT)   /* diese Zeile wurde ggf. gelesen  und dann kam */
    next = 1;                   /* EOF -> kein Check mehr, next=0... */
  if (f && !next)
    anerror(errtxt[MISSINGNEXT]);
}

void addtoken(tnode * root, const char *str, int id)
{
  static struct replace {
    char c;
    char *str;
  } replace[] = {
    {
    0xe4, "ae"}, {
    0xc4, "ae"}, {
    0xf6, "oe"}, {
    0xd6, "oe"}, {
    0xfc, "ue"}, {
    0xdc, "ue"}, {
    0xdf, "ss"}, {
    0, 0}
  };
  if (root->id >= 0 && root->id != id && !root->leaf)
    root->id = -1;
  if (!*str) {
    root->id = id;
    root->leaf = 1;
  } else {
    char c = (char)tolower(*str);
    int lindex = ((unsigned char)c) % 32;
    int i = 0;
    tnode *tk = root->next[lindex];

    if (root->id < 0)
      root->id = id;
    while (tk && tk->c != c)
      tk = tk->nexthash;
    if (!tk) {
      tk = (tnode *) calloc(1, sizeof(tnode));
      tk->id = -1;
      tk->c = c;
      tk->nexthash = root->next[lindex];
      root->next[lindex] = tk;
    }
    addtoken(tk, str + 1, id);
    while (replace[i].str) {
      if (*str == replace[i].c) {
        char zText[1024];

        strcat(strcpy(zText, replace[i].str), str + 1);
        addtoken(root, zText, id);
        break;
      }
      i++;
    }
  }
}

void inittokens(void)
{
  int i;
  t_item *it;
  t_names *n;
  t_skills *sk;
  t_spell *sp;
  t_liste *l;
  t_direction *d;
  t_params *p;
  t_keyword *kw;

  for (i = 0, it = itemdata; it; it = it->next, ++i)
    for (n = it->name; n; n = n->next)
      addtoken(&tokens[UT_ITEM], n->txt, i);

  for (p = parameters; p; p = p->next)
    addtoken(&tokens[UT_PARAM], p->name, p->param);

  for (i = 0, sk = skilldata; sk; sk = sk->next, ++i)
    addtoken(&tokens[UT_SKILL], sk->name, i);

  for (i = 0, sp = spells; sp; sp = sp->next, ++i)
    addtoken(&tokens[UT_SPELL], sp->name, i);

  for (i = 0, d = directions; d; d = d->next, ++i)
    addtoken(&tokens[UT_DIRECTION], d->name, d->dir);

  for (kw = keywords; kw; kw = kw->next)
    addtoken(&tokens[UT_KEYWORD], kw->name, kw->keyword);

  for (i = 0, l = buildingtypes; l; l = l->next, ++i)
    addtoken(&tokens[UT_BUILDING], l->name, i);

  for (i = 0, l = shiptypes; l; l = l->next, ++i)
    addtoken(&tokens[UT_SHIP], l->name, i);

  for (i = 0, l = herbdata; l; l = l->next, ++i)
    addtoken(&tokens[UT_HERB], l->name, i);

  for (i = 0, l = potionnames; l; l = l->next, ++i)
    addtoken(&tokens[UT_POTION], l->name, i);

  for (i = 0, l = Rassen; l; l = l->next, ++i)
    addtoken(&tokens[UT_RACE], l->name, i);

  for (i = 0, l = options; l; l = l->next, ++i)
    addtoken(&tokens[UT_OPTION], l->name, i);
}

int main(int argc, char *argv[])
{
  int i, faction_count = 0, unit_count = 0, nextarg = 1;

#if macintosh
  argc = ccommand(&argv);       /* consolenabruf der parameter fuer  macintosh added 15.6.00 chartus */
#endif

  set_output(stdout, stderr);
  for (i = 0; i < MAX_ERRORS; i++)
    errtxt[i] = Errors[i];      /* mit Defaults besetzten, weil NULL ->  crash */

  /*
   * Path-Handling
   */
  path = getenv("ECHECKPATH");
  if (!path)
    path = DEFAULT_PATH;
  path = strdup(path);
  set_output(NULL, stdout);

  filename = getenv("ECHECKOPTS");
  if (filename)
    parse_options(filename, 1);

  if (argc <= 1)
    printhelp(argc, argv, 0);

  if (argc > 1)
    nextarg = check_options(argc, argv, 1, 1);

  readfiles(1);

  if (!(filesread & HAS_MESSAGES)) {
    fprintf(ERR, "\n  **  ECheck V%s, %s  **\n\n"
      " kann keine Datei lesen!  -  can't read any file!\n\n Pfad / Path: '%s/%s'\n\n",
      echeck_version, __DATE__, path, echeck_locale);
    help_keys('f');             /* help_keys() macht exit() */
  }

  if (!compile)
    fprintf(ERR, errtxt[ECHECK], echeck_version, __DATE__);

  if (filesread != HAS_ALL) {
    strcpy(checked_buf, errtxt[MISSINGFILES]);
    if (!(filesread & HAS_PARAM)) {
      Scat(errtxt[MISSFILEPARAM]);
    }
    if (!(filesread & HAS_ITEM)) {
      Scat(errtxt[MISSFILEITEM]);
    }
    if (!(filesread & HAS_SKILL)) {
      Scat(errtxt[MISSFILESKILL]);
    }
    if (!(filesread & HAS_KEYWORD)) {
      Scat(errtxt[MISSFILECMD]);
    }
    if (!(filesread & HAS_DIRECTION)) {
      Scat(errtxt[MISSFILEDIR]);
    }
    if (!(filesread & HAS_MESSAGES)) {
      Scat(errtxt[MISSFILEMSG]);
    }
    fputs(checked_buf, ERR);
    return 5;
  }

  inittokens();

  if (run_tests) {
#ifdef WITH_CUTEST
    int fails = 0;
    CuSuite *suite = CuSuiteNew();
    CuString *output = CuStringNew();

    AddTestSuites(suite, run_tests);
    CuSuiteRun(suite);
    CuSuiteSummary(suite, output);
    CuSuiteDetails(suite, output);
    printf("%s\n", output->buffer);
    CuStringDelete(output);

    fails = suite->failCount;
    CuSuiteDelete(suite);

    cleanup();
    return fails;
#else
    cleanup();
    return 0;
#endif
  }

  F = stdin;

  for (i = nextarg; i < argc; i++) {
    F = fopen(argv[i], "r");
    if (!F) {
      fprintf(ERR, errtxt[CANTREADFILE], argv[i]);
      return 2;
    } else {
      filename = argv[i];
      if (!compile) {
        fprintf(ERR, errtxt[PROCESSINGFILE], argv[i]);
        if (!compact)
          fputc('\n', ERR);
      } else
        fprintf(ERR, "%s|version|%s|%s\n", filename, echeck_version, __DATE__);
      process_order_file(&faction_count, &unit_count);
    }
  }
  /*
   * Falls es keine input Dateien gab, ist F immer noch auf stdin
   * gesetzt
   */
  if (F == stdin)
    process_order_file(&faction_count, &unit_count);

  if (compile) {
    fprintf(ERR, "%s|warnings|%d\n%s|errors|%d\n", filename,
      warning_count, filename, error_count);
    return 0;
  }

  fprintf(ERR, errtxt[ORDERSREAD],
          faction_count,
          faction_count != 1 ? errtxt[FACTIONS] : errtxt[FACTION],
          unit_count, unit_count != 1 ? errtxt[UNITS] : errtxt[UNIT]);

  if (unit_count == 0) {
    fputs(errtxt[CHECKYOURORDERS], ERR);
    return -42;
  }

  if (!error_count && !warning_count && faction_count && unit_count)
    fputs(errtxt[ORDERSOK], ERR);

  if (error_count > 1)
    fprintf(ERR, errtxt[FOUNDERRORS], error_count);
  else if (error_count == 1)
    fputs(errtxt[FOUNDERROR], ERR);

  if (warning_count) {
    if (error_count)
      fputs(errtxt[AND], ERR);
    else
      fputs(errtxt[THERE], ERR);
  }

  if (warning_count > 1) {
    if (!error_count)
      fputs(errtxt[WERE], ERR);
    fprintf(ERR, errtxt[WARNINGS], warning_count);
  } else if (warning_count == 1) {
    if (!error_count)
      fputs(errtxt[WAS], ERR);
    fputs(errtxt[AWARNING], ERR);
  }

  if (warning_count || error_count)
    fputs(errtxt[DISCOVERED], ERR);
  return 0;
}
