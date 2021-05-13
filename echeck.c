/*
 * Eressea PBeM Order Syntax Checker Copyright 1997-2021
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

#if defined(__unix__) || defined(__APPLE__)
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#include <unistd.h>

#include <sys/stat.h>
#define STAT stat

#ifdef _POSIX_VERSION
#include <libgen.h>
#endif
#endif /* __unix__ */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#define STAT _stat
#endif

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

#include "unicode.h"
#include "whereami.h"

#ifdef HAVE_GETTEXT
#include <locale.h>
#include <libintl.h>
#else
#define gettext(X) (X)
#define ngettext(S, P, N) (((N) == 1) ? S : P)
#endif

#define _(str) gettext(str)
#define t(str) (str)

/* platform-specific defines */
#ifdef WIN32
#define PATH_DELIM ";"
#else
#define PATH_DELIM ":"
#endif

/* string.h overrides */
#if defined(_MSC_VER)
#define STRICMP(a, b) _stricmp(a, b)
#define STRNICMP(a, b, n) _strnicmp(a, b, n)
#define STRDUP(a) _strdup(a)
#define SNPRINTF _snprintf
#else
#if __GNUC__
#define STRICMP(a, b) strcasecmp(a, b)
#define STRNICMP(a, b, n) strncasecmp(a, b, n)
#else
#define STRICMP(a, b) stricmp(a, b)
#define STRNICMP(a, b, n) strnicmp(a, b, n)
#endif
#define STRDUP(a) strdup(a)
#define SNPRINTF snprintf
#endif

#include <string.h>

static const char *echeck_version = "4.6.4";

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
  UT_MAX
};

static char *Keys[UT_MAX] = {
  "",       "PARAM", "ITEM",  "SKILL", "KEYWORD", "BUILDING",  "HERB",
  "POTION", "RACE",  "SPELL", "SHIP",  "OPTION",  "DIRECTION",
};

/*
 * Diese Dinge müssen geladen sein, damit es überhaupt klappt
 */
#define HAS_PARAM 0x01
#define HAS_ITEM 0x02
#define HAS_SKILL 0x04
#define HAS_KEYWORD 0x08
#define HAS_DIRECTION 0x10
#define HAS_ALL 0x1F

typedef struct _ech_file {
  const char *name;
  int type;
} t_ech_file;

const t_ech_file ECheck_Files[] = {
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
  {"tokens.txt", -1}};

const int filecount = sizeof(ECheck_Files) / sizeof(ECheck_Files[0]);

#define SPACE_REPLACEMENT '~'
#define SPACE ' '
#define ESCAPE_CHAR '\\'
#define COMMENT_CHAR ';'
#define MARGIN 78
#define RECRUIT_COST 50

#define INDENT_FACTION 0
#define INDENT_UNIT 2
#define INDENT_ORDERS 4
#define INDENT_NEW_ORDERS 6

#define BUFSIZE 8192
#define MAXLINE 4096
#define DESCRIBESIZE 4095
#define NAMESIZE 127

FILE *ERR, *OUT = 0;

int line_no, /* count line number */
  filesread = 0;

int echo_it = 0,    /* option: echo input lines */
  no_comment = -3,   /* Keine Infos in [] hinter EINHEIT */
  show_warnings = 4, /* option: print warnings (levels) */
  warnings_cl = 0,   /* -w auf der Kommandozeile gegeben */
  warn_off = 0,      /* ECHECK NOWARN */
  use_stderr = 0,    /* option: use stderr for errors etc */
  brief = 0,         /* option: don't list errors */
  ignore_NameMe = 0, /* option: ignoriere NameMe-Kommentare ;; */
  piping = 0,        /* option: wird output als pipe-input  benutzt? */
  lohn = 10,         /* Lohn für Arbeit - je Region zu setzen */
  silberpool = 1,    /* option: Silberpool-Verwaltung */
  line_start = 0,    /* option: Beginn der Zeilenzählung */
  noship = 0, noroute = 0, nolost = 0, bang_cmd = 0, at_cmd = 0,
     attack_warning = 0,
     compile = 0;          /* option: compiler-/magellan-style  warnings */
int error_count = 0,       /* counter: errors */
  warning_count = 0;       /* counter: warnings */

const char *echeck_locale = "de", *echeck_rules = "e2";
char *filename;

static char order_buf[BUFSIZE], /* current order line */
  checked_buf[BUFSIZE],    /* checked order line */
  message_buf[BUFSIZE],    /* messages are composed here */
  warn_buf[BUFSIZE],       /* warnings are composed here */
  indent, next_indent,     /* indent index */
  does_default = 0,        /* Ist DEFAULT aktiv? */
  befehle_ende;            /* EOF der Befehlsdatei */
int rec_cost = RECRUIT_COST,
                       this_command,
    this_unit, /* wird von getaunit gesetzt */
  Rx, Ry;      /* Koordinaten der aktuellen Region */
static char *g_path;
static const char *g_basedir;
FILE *F;

enum { OUT_NORMAL, OUT_COMPILE, OUT_MAGELLAN };

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
  K_GROUP,
  K_SORT,
  K_PREFIX,
  K_PROMOTION,
  K_PROMOTE,
  K_LANGUAGE,
  MAXKEYWORDS
};

static const char *Keywords[MAXKEYWORDS] = {
  "ALLIANCE",  "PAY",         "WORK",       "ATTACK",   "STEAL",    "SIEGE",
  "NAME",      "USE",         "DESCRIBE",   "ENTER",    "GUARD",    "MESSAGE",
  "END",       "RIDE",        "FOLLOW",     "RESEARCH", "GIVE",     "HELP",
  "FIGHT",     "COMBATMAGIC", "BUY",        "CONTACT",  "TEACH",    "STUDY",
  "MAKE",      "MOVE",        "PASSWORD",   "PLANT",    "RECRUIT",  "REPORT",
  "OPTION",    "SPY",         "SETSTEALTH", "CARRY",    "QUIT",     "TAX",
  "ENTERTAIN", "SELL",        "LEAVE",      "CAST",     "SHOW",     "DESTROY",
  "FORGET",    "DEFAULT",     "COMMENT",    "ROUTE",    "SABOTAGE", "BREED",
  "ORIGIN",    "EMAIL",       "RESERVE",    "CLAIM",    "BANNER",   "NUMBER",
  "SCHOOL",    "PIRACY",      "GROUP",      "SORT",     "PREFIX",   "PROMOTION",
  "PROMOTE",   "LANGUAGE"};

typedef struct _keyword {
  struct _keyword *next;
  char *name;
  int keyword;
} t_keyword;

t_keyword *keywords = NULL;

#define igetkeyword(s) findtoken(igetstr(s), UT_KEYWORD)

static const char *magiegebiet[] = {"Illaun", "Tybied", "Cerddor", "Gwyrrd",
                                    "Draig"};

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
  P_NEXT,
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
  P_LOCALE,
  P_BEFORE,
  P_AFTER,
  P_ALLIANCE,
  P_AUTO,
  MAXPARAMS
};

static const char *Params[MAXPARAMS] = {
  "ALL",     "EACH",           "PEASANTS",  "CASTLE",  "BUILDING",
  "UNIT",    "FLEE",           "REAR",      "FRONT",   "CONTROL",
  "HERBS",   "TREES",          "NOT",       "NEXT",    "FACTION",
  "PERSON",  "REGION",         "SHIP",      "SILVER",  "ROAD",
  "TEMP",    "PRIVATE",        "FIGHT",     "OBSERVE", "GIVE",
  "GUARD",   "FACTIONSTEALTH", "WARN",      "LEVEL",   "HORSES",
  "FOREIGN", "AGGRESSIVE",     "DEFENSIVE", "NUMBER",  "LOCALE",
  "BEFORE",  "AFTER",          "ALLIANCE",  "AUTO"};

typedef struct _params {
  struct _params *next;
  char *name;
  int param;
} t_params;

t_params *parameters = NULL;

static const char *reports[] = {/* Fehler und Meldungen im Report */
                                "Kampf",         "Ereignisse", "Bewegung",
                                "Einkommen",     "Handel",     "Produktion",
                                "Orkvermehrung", "Alles"};

static const int MAXREPORTS = sizeof(reports) / sizeof(reports[0]);

static const char *message_levels[] = {"Wichtig", "Debug", "Fehler",
                                       "Warnungen", "Infos"};

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
  "EAST", "WEST", "PAUSE", "NORTHEAST", "NORTHWEST", "SOUTHEAST", "SOUTHWEST"};

t_direction *directions = NULL;

t_liste *Rassen = NULL;

t_liste *shiptypes = NULL;

t_liste *herbdata = NULL;

enum {
  ECHECK,
  ENDWITHOUTTEMP,
  ERRORCOORDINATES,
  ERRORHELP,
  ERRORLEVELPARAMETERS,
  ERRORREGION,
  ERRORREGIONPARAMETER,
  FACTION,
  FACTIONS,
  FACTION0USED,
  FACTIONINVALID,
  FACTIONMISSING,
  FOLLOW,
  INTERNALCHECK,
  INVALIDEMAIL,
  MISSFILEPARAM,
  MISSFILECMD,
  MISSFILEITEM,
  MISSFILESKILL,
  MISSFILEDIR,
  MISSINGFACTIONNUMBER,
  MISSINGNUMRECRUITS,
  MISSINGOFFER,
  MISSINGPASSWORD,
  MISSINGUNITNUMBER,
  NAMECONTAINSBRACKETS,
  NEEDBOTHCOORDINATES,
  NOCARRIER,
  NOFIND,
  NOSEND,
  NOTEMPNUMBER,
  NTOOBIG,
  NUMCASTLEMISSING,
  NUMMISSING,
  OBJECTNUMBERMISSING,
  ORDERNUMBER,
  ORDERSREAD,
  PASSWORDCLEARED,
  PASSWORDMSG2,
  POST,
  PRE,
  PROCESSINGFILE,
  QUITMSG,
  RECRUITCOSTSSET,
  RESERVE0SENSELESS,
  ROUTENOTCYCLIC,
  SCHOOLCHOSEN,
  SORT,
  UNITALREADYHAS,
  UNITALREADYHASMOVED,
  UNITALREADYHASORDERS,
  UNITHASNTPERSONS,
  UNITHASPERSONS,
  UNITMISSPERSON,
  UNITMISSSILVER,
  UNITMOVESSHIP,
  UNITMUSTBEONSHIP,
  UNITONSHIPHASMOVED,
  MAX_ERRORS
};

static const char *Errors[MAX_ERRORS] = {
  "ECHECK",
  "ENDWITHOUTTEMP",
  "ERRORCOORDINATES",
  "ERRORHELP",
  "ERRORLEVELPARAMETERS",
  "ERRORREGION",
  "ERRORREGIONPARAMETER",
  "FACTION",
  "FACTIONS",
  "FACTION0USED",
  "FACTIONINVALID",
  "FACTIONMISSING",
  "FOLLOW",
  "INTERNALCHECK",
  "INVALIDEMAIL",
  "MISSFILEPARAM",
  "MISSFILECMD",
  "MISSFILEITEM",
  "MISSFILESKILL",
  "MISSFILEDIR",
  "MISSINGFACTIONNUMBER",
  "MISSINGNUMRECRUITS",
  "MISSINGOFFER",
  "MISSINGPASSWORD",
  "MISSINGUNITNUMBER",
  "NAMECONTAINSBRACKETS",
  "NEEDBOTHCOORDINATES",
  "NOCARRIER",
  "NOFIND",
  "NOSEND",
  "NOTEMPNUMBER",
  "NTOOBIG",
  "NUMCASTLEMISSING",
  "NUMMISSING",
  "OBJECTNUMBERMISSING",
  "ORDERNUMBER",
  "ORDERSREAD",
  "PASSWORDCLEARED",
  "PASSWORDMSG2",
  "POST",
  "PRE",
  "PROCESSINGFILE",
  "QUITMSG",
  "RECRUITCOSTSSET",
  "RESERVE0SENSELESS",
  "ROUTENOTCYCLIC",
  "SCHOOLCHOSEN",
  "SORT",
  "UNITALREADYHAS",
  "UNITALREADYHASMOVED",
  "UNITALREADYHASORDERS",
  "UNITHASNTPERSONS",
  "UNITHASPERSONS",
  "UNITMISSPERSON",
  "UNITMISSSILVER",
  "UNITMOVESSHIP",
  "UNITMUSTBEONSHIP",
  "UNITONSHIPHASMOVED",
};

typedef struct _names {
  struct _names *next;
  char *txt;
} t_names;

typedef struct _item {
  struct _item *next;
  t_names *name;
  int preis; /* für Luxusgüter */
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
#define SP_KAMPF 2
#define SP_POST 4
#define SP_PRAE 8
#define SP_BATTLE 14 /* 2+4+8 */
#define SP_ALL 15

t_liste *potionnames = NULL;

t_liste *buildingtypes = NULL;

enum { POSSIBLE, NECESSARY };

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
  int ship; /* Nummer unseres Schiffes; ship<0: ich  bin owner */
  char lives, hasmoved;
  t_region *region;
  int newx, newy;
  int transport; /* hier steht drin, welche Einheit mich  CARRYIEREn wird */
  int drive;     /* hier steht drin, welche Einheit mich  CARRYIEREn soll */
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
  int spell; /* Bit-Map: 2^Spell-Typ */
} unit;

typedef struct teach {
  struct teach *next;
  unit *teacher;
  unit *student;
} teach;

teach *teachings = NULL;

static unit *units = NULL;

static unit *order_unit, /* Die Einheit, die gerade dran ist */
  *mother_unit,          /* Die Einheit, die MACHE TEMP macht */
  *cmd_unit; /* Die Einheit, die gerade angesprochen  wird, z.B. mit GIB */

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
int findstr(const char *v[], const char *s, int max);

#define addlist(l, p) (p->next = *l, *l = p)

int Pow(int p) {
  if (!p)
    return 1;
  else
    return 2 << (p - 1);
}

#define iswxspace(c) (c == 160 || iswspace(c))

char *eatwhite(char *ptr) {
  while (*ptr) {
    if (*ptr > 0 && isspace(*ptr)) {
      ++ptr;
    } else {
      ucs4_t ucs;
      size_t size = 0;
      int ret = unicode_utf8_to_ucs4(&ucs, (utf8_t *)ptr, &size);
      if (ret != 0 || !iswxspace((wint_t)ucs)) {
        break;
      }
      ptr += size;
    }
  }
  return ptr;
}

static char nulls[] = "\0\0\0\0\0\0\0\0";

const char *itob(int i) {
  char *dst;
  int b = i, x;

  if (i == 0)
    return "0";

  dst = nulls + 6;
  do {
    x = b % 36;
    b /= 36;
    dst--;
    if (x < 10)
      *dst = (char)('0' + x);
    else
      *dst = (char)('a' + (x - 10));
  } while (b > 0);
  return dst;
}

#define scat(X) strcat(checked_buf, X)
#define Scat(X)                                                                \
  do {                                                                         \
    scat(" ");                                                                 \
    scat(X);                                                                   \
  } while (0)

void qcat(const char *s) {
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

void icat(int n) {
  static char s[10];

  sprintf(s, " %d", n);
  scat(s);
}

void bcat(int n) {
  static char s[10];

  sprintf(s, " %s", itob(n));
  scat(s);
}

int ItemPrice(int i) {
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
              "Internal Error: ItemPrice %d (%d) not found!\n",
              ino, i, ino, i);
      exit(123);
    }
    item = it;
  }
  return item->preis;
}

char *ItemName(int i, int plural) {
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
              "Internal Error: ItemName %d (%d) not found!\n",
              ino, i, ino, i);
      exit(123);
    }
    item = it;
  }
  if (plural)
    return item->name->next->txt;
  return item->name->txt;
}

FILE *path_fopen(const char *path_par, const char *file, const char *mode) {
  char *pathw = STRDUP(path_par);
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

char *transliterate(char *out, size_t size, const char *in) {
  const utf8_t *src = (const utf8_t *)in;
  char *dst = out;

  --size; /* need space for a final 0-byte */
  while (*src && size) {
    size_t len;
    const utf8_t *p = src;
    while ((p + size > src) && *src && (~*src & 0x80)) {
      *dst++ = (char)tolower(*src++);
    }
    len = src - p;
    size -= len;
    while (size > 0 && *src && (*src & 0x80)) {
      unsigned int advance = 2;
      if (src[0] == (utf8_t)'\xc3') {
        if (src[1] == (utf8_t)'\xa4' || src[1] == (utf8_t)'\x84') {
          memcpy(dst, "ae", 2);
        } else if (src[1] == (utf8_t)'\xb6' || src[1] == (utf8_t)'\x96') {
          memcpy(dst, "oe", 2);
        } else if (src[1] == (utf8_t)'\xbc' || src[1] == (utf8_t)'\x9c') {
          memcpy(dst, "ue", 2);
        } else if (src[1] == (utf8_t)'\x9f') {
          memcpy(dst, "ss", 2);
        } else {
          advance = 0;
        }
      } else if (src[0] == (utf8_t)'\xe1') {
        if (src[1] == (utf8_t)'\xba' && src[2] == (utf8_t)'\x9e') {
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
void readspell(char *s) {
  char *x;
  t_spell *sp;
  char buffer[128];

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
  sp = (t_spell *)calloc(1, sizeof(t_spell));
  if (sp) {
    sp->name = STRDUP(transliterate(buffer, sizeof(buffer), s));
    if (x) {
      s = (char *)eatwhite(x + 1);
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
}

static void readskill(char *s) { /* parsed einen String nach Talenten */
  char buffer[128];
  char *x;
  t_skills *sk;

  sk = (t_skills *)calloc(1, sizeof(t_skills));
  if (!sk) {
    return;
  }
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
  sk->name = STRDUP(transliterate(buffer, sizeof(buffer), s));
  if (x) {
    s = x + 1;
    while (*s > 0 && isspace(*s))
      ++s;
    if (*s)
      sk->kosten = atoi(s);
  }
  sk->next = skilldata;
  skilldata = sk;
}

int readitem(char *s) { /* parsed einen String nach Items */
  char buffer[128];
  char *x = s;
  t_item *it = (t_item *)calloc(1, sizeof(t_item));

  if (it) {
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
      if (atoi(s) > 0) /* Name, 12 -> Luxusgut "Name", EK-Preis *   12 */
        it->preis = atoi(s);
      else {
        t_names *n = (t_names *)calloc(1, sizeof(t_names));
        if (n) {
          n->txt = STRDUP(transliterate(buffer, sizeof(buffer), s));
          n->next = it->name;
        }
        it->name = n;
      }
      if (x) {
        s = (char *)eatwhite(x + 1);
      }
    } while (x && *s);
    if (!it->name) {
      free(it);
      return 0;
    }
    it->next = itemdata;
    itemdata = it;
  }
  return 1;
}

void readliste(char *s,
               t_liste **L) { /* parsed einen String nach einem Token */
  char *x;
  t_liste *ls;
  char buffer[128];

  x = strchr(s, '\n');
  if (x)
    *x = 0;
  ls = (t_liste *)calloc(1, sizeof(t_liste));
  if (ls) {
    ls->name = STRDUP(transliterate(buffer, sizeof(buffer), s));
    addlist(L, ls);
  }
}

int readkeywords(char *s) { /* parsed einen String nach Befehlen */
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
    if (STRICMP(s, Keywords[i]) == 0) {
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
  k = (t_keyword *)calloc(1, sizeof(t_keyword));
  if (!k) {
    return 0;
  }
  k->name = STRDUP(transliterate(buffer, sizeof(buffer), s));
  k->keyword = i;
  k->next = keywords;
  keywords = k;
  return 1;
}

int readparams(char *s) { /* parsed einen String nach Parametern */
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
    if (STRICMP(s, Params[i]) == 0)
      break;
  if (i == MAXPARAMS)
    return 0;
  s = eatwhite(x + 1);
  if (!*s)
    return 0;
  x = strchr(s, '\n');
  if (x)
    *x = 0;
  p = (t_params *)calloc(1, sizeof(t_params));
  if (!p) {
    return 0;
  }
  p->name = STRDUP(transliterate(buffer, sizeof(buffer), s));
  p->param = i;
  p->next = parameters;
  parameters = p;
  return 1;
}

int readdirection(char *s) { /* parsed einen String nach Richtungen */
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
    if (STRICMP(s, Directions[i]) == 0)
      break;
  if (i == MAXDIRECTIONS)
    return 0;
  s = eatwhite(x + 1);
  if (!*s)
    return 0;
  x = strchr(s, '\n');
  if (x)
    *x = 0;
  d = (t_direction *)calloc(1, sizeof(t_direction));
  if (!d) {
    return 0;
  }
  d->name = STRDUP(transliterate(buffer, sizeof(buffer), s));
  d->dir = i;
  d->next = directions;
  directions = d;
  return 1;
}

int parsefile(char *s, int typ) { /* ruft passende Routine auf */
  int ok; /* nicht alle Zeilenparser geben Wert  zurück */
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

  default:              /* tokens.txt */
    x = strchr(s, ':'); /* TOKEN: werte */
    if (!x)
      return 0;
    y = x + 1;
    while (*(x - 1) > 0 && isspace(*(x - 1)))
      --x;
    *x = '\0';
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
static void macify(unsigned char *s) {

  unsigned char translate[][2] = {{128, 196}, /* Adieresis */
                                  {133, 214}, /* Odieresis */
                                  {134, 220}, /* Udieresis */
                                  {167, 223}, /* germandbls */
                                  {138, 228}, /* adieresis */
                                  {154, 246}, /* odieresis */
                                  {159, 252}, /* udieresis */
                                  {0, 0}};
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

void set_order_unit(unit *u) { order_unit = u; }

void mock_input(const char *input) {
  free(mocked_input);
  mocked_input = STRDUP(input);
  mock_pos = mocked_input;
}

int get_long_order_line()
{
  if (order_unit)
    return order_unit->long_order_line;
  else
    return -1;
}

static char *fgetbuffer(char *buf, int size, FILE *F) {
  if (mocked_input) {
    ptrdiff_t bytes;
    char *nextbr;
    if (!*mock_pos)
      return 0;
    nextbr = strchr(mock_pos, '\n');
    if (!nextbr) {
      nextbr = mock_pos + strlen(mock_pos);
    } else {
      ++nextbr;
    }
    bytes = (ptrdiff_t)size - 1;
    if (bytes > nextbr - mock_pos) {
      bytes = nextbr - mock_pos;
    }
    if (bytes) {
      memcpy(buf, mock_pos, bytes);
    }
    buf[bytes] = 0;
    mock_pos += bytes;
    return buf;
  }
  return fgets(buf, size, F);
}

void readafile(const char *fn, int typ) {
  int ok, line;
  FILE *F;
  char *s, *x;

  F = path_fopen(g_basedir, fn, "rt");
  if (!F)
    return;
  for (line = 1;; line++) {
    do {
      s = fgets(order_buf, MAXLINE, F);
    } while (
      !feof(F) && s &&
      (*s == '#' || *s == '\n')); /* Leer- und  Kommentarzeilen   überlesen */
    if (feof(F) || !s) {
      fclose(F);
      return;
    }
    x = strchr(s, '\n');
    if (x)
      *x = 0; /* \n am Zeilenende löschen */
    ok = parsefile(s, typ);
    if (!ok)
      fprintf(ERR,
              "Fehler in Datei %s Zeile %d: `%s'\n"
              "Error in file %s line %d: `%s'\n",
              fn, line, s, fn, line, s);
  }
}

void porder(void) {
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

int
this_unit_id(void)
{
  return order_unit ? order_unit->no : 0;
}

#define LOG_ERROR 0

void
log_message_va(
  int level, 
  const char *file,
  int line, 
  const char *order,
  int unit_id, 
  const char *format, va_list va)
{
  char bf[65];

  if (brief) return;

  if (!order)
    strcpy(bf, "...");
  else
    strncpy(bf, order, 64);
  strcpy(bf + 61, "..."); /* zu lange Befehle ggf. kürzen */

  if (level == LOG_ERROR) {
    ++error_count;
    if (compile == OUT_NORMAL) {
      fprintf(ERR, _("Error in line %d"), line);
    }
  }
  else if (level > show_warnings) {
    return;
  }
  else {
    ++warning_count;
    if (compile == OUT_NORMAL) {
      fprintf(ERR, _("Warning in line %d"), line);
    }
  }
  switch (compile) {
  case OUT_MAGELLAN:
    fprintf(ERR, "%s|%d|%d|", filename, line, level);
    vfprintf(ERR, format, va);
    fprintf(ERR, ". '%s'\n", bf);
    break;
  case OUT_COMPILE:
    fprintf(ERR, "%s:%d|%d|%s|", filename, line, level, itob(unit_id)); 
    vfprintf(ERR, format, va);
    fprintf(ERR, "|%s\n", bf);
    break;
  default:
    fputs(": ", ERR);
    vfprintf(ERR, format, va);
    fprintf(ERR, ".\n  '%s'\n", bf);
    break;
  }
}

void
log_error(
  const char *file,
  int line,
  const char *order,
  int unit_id,
  const char *format, ...)
{
  va_list va;
  va_start(va, format);
  log_message_va(LOG_ERROR, file, line, order, unit_id, format, va);
  va_end(va);
}

void
log_warning(
  int level,
  const char *file,
  int line,
  const char *order,
  int unit_id,
  const char *format, ...)
{
  va_list va;
  if (warn_off) {
    return;
  }
  va_start(va, format);
  log_message_va(level, file, line, order, unit_id, format, va);
  va_end(va);
}

int btoi(char *s) {
  char *x = s;
  int i = 0;

  assert(s);
  if (!(*s))
    return 0;
  while (isalnum(*s))
    s++;
  *s = 0;
  s = x;
  if (strlen(s) > 4) {
    log_error(filename, line_no, order_buf, this_unit_id(),
              _("Number too big: '%s'. Using 1 instead"), s);
    return 1;
  }
#ifdef HAVE_STRTOL
  i = (int)(strtol(x, NULL, 36));
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

const char *uidbuf(const unit *u, char *bf) {
  sprintf(bf, "%s%s", u->temp != 0 ? "TEMP " : "", itob(u->no));
  return bf;
}

const char *uid1(const unit *u) {
  static char bf[18];
  return uidbuf(u, bf);
}

const char *uid2(const unit *u) {
  static char bf[18];
  return uidbuf(u, bf);
}

const char *uid3(const unit *u) {
  static char bf[18];
  return uidbuf(u, bf);
}

const char *uid(const unit *u) {
  static int toggle;
  toggle = 2 - toggle;
  if (toggle == 1) {
    return uid1(u);
  } else if (toggle == 2) {
    return uid2(u);
  }
  return uid3(u);
}

const char *Uid(int i) {
  unit *u;

  u = find_unit(i, 0);
  if (!u)
    u = find_unit(i, 1);
  if (!u) {
    u = newunit(-1, 0);
  }
  return uid(u);
}

void warning(const char *s, int line, char *order, char level) {
  log_warning(level, filename, line, order, this_unit_id(), s);
}

#define awarning(s, level) warning(s, line_no, order_buf, level)
#define dwarning(s, level) warning_deprecated(s, line_no, order_buf, level)

static const struct warning {
  const char *token;
  const char *message;
} warnings[] = {
  {"FOLLOW", t("FOLLOW UNIT xx, FOLLOW SHIP xx or FOLLOW")},
  {"INTERNALCHECK", t("<internal check>")},
  {"INVALIDEMAIL", t("invalid email address")},
  {"MISSFILEPARAM", t("parameters")},
  {"MISSFILECMD", t("commands")},
  {"MISSFILEITEM", t("items")},
  {"MISSFILESKILL", t("skills")},
  {"MISSFILEDIR", t("directions")},
  {"MISSFILEMSG", t("messages")},
  {"MISSINGQUOTES", t("Missing \"")},
  {"MISSINGFACTIONNUMBER", t("Missing faction number")},
  {"MISSINGNUMRECRUITS", t("Number of recruits missing")},
  {"MISSINGOFFER", t("Missing offer")},
  {"MISSINGPASSWORD", t("Missing password")},
  {"MISSINGUNITNUMBER", t("Missing unit number")},
  {"NAMECONTAINSBRACKETS", t("Names must not contain brackets")},
  {"NEEDBOTHCOORDINATES", t("Both coordinated must be supplied")},
  {"NOCARRIER", t("Can't find unit to carry")},
  {"NOFIND", t("FIND has been replaced by OPTION ADDRESSES")},
  {"NOSEND", t("SEND has been renamed into OPTION")},
  {"NOTEMPNUMBER", t("No TEMPORARY number")},
  {"NUMBERNOTPOSSIBLE", t("Number is not possible here")},
  {"NUMCASTLEMISSING", t("Number of castle missing")},
  {"NUMLUXURIESMISSING", t("Number of luxuries missing")},
  {"NUMMISSING", t("Number of items/men/silver missing")},
  {"OBJECTNUMBERMISSING", t("number of object missing")},
  {"ORDERNUMBER",
   t("NUMBER SHIP, NUMBER CASTLE, NUMBER FACTION or NUMBER UNIT")},
  {"PASSWORDCLEARED", t("Password cleared")},
  {"PASSWORDMSG2", t("\n\n  ****  A T T E N T I O N !  ****\n\n  ****  "
                     "Password missing!  ****\n\n")},
  {"POST", t("post-")},
  {"PRE", t("pre-")},
  {"PROCESSINGFILE", t("Processing file '%s'.")},
  {"QUITMSG", t("Attention! QUIT found! Your faction will be cancelled!")},
  {"RESERVE0SENSELESS", t("RESERVE 0 xxx doesn't make any sense")},
  {"ROUTENOTCYCLIC", t("ROUTE is not cyclic; (%d,%d) -> (%d,%d)")},
  {"SCHOOLCHOSEN", t("School '%s' chosen.\n")},
  {"SORT", t("SORT BEFORE or BEHIND <unit>")},
  {"TEMPHASNTPERSONS",
   t("Unit TEMPORARY %s hasn't got men and hasn't recruited anyone")},
  {"TEMPNOTTEMP",
   t("Unit TEMPORARY %s hasn't been generated with MAKE TEMPORARY")},
  {"TEMPUNITSCANTRESERVE",
   t("TEMPORARY units can't use RESERVE! Use GIVE instead!")},
  {"TEMPUNITSCANTGIVE",
   t("TEMPORARY units can't use GIVE, it happens before MAKE!")},
  {"UNITALREADYHAS", t("Unit %s already has a ")},
  {"UNITALREADYHASLONGORDERS",
   t("Unit %s already has a long order in line %d (%s)")},
  {"UNITALREADYHASMOVED", t("Unit %s already has moved")},
  {"UNITALREADYHASORDERS", t("Unit %s already has got orders in line %d")},
  {"UNITCANSTILLTEACH", t("Unit %s can teach %d more students")},
  {"UNITHASNTPERSONS", t("Unit TEMPORARY %s hasn't recruited and hasn't got "
                         "any men! It may lose silver and/or items")},
  {"UNITHASPERSONS", t("Unit %s has %d men")},
  {"UNITMISSPERSON", t("Unit %s may have not enough men")},
  {"UNITMISSSILVER", t("Unit %s may have not enough silver")},
  {"UNITMOVESSHIP", t("Unit %s moves ship %s and may lack control")},
  {"UNITMUSTBEONSHIP",
   t("Unit must be in a castle, in a building or on a ship")},
  {"UNITNOTONSHIPBUTONSHIP", t("Unit %s may be on ship %s instead of ship %s")},
  {"UNITONSHIPHASMOVED", t("Unit %s on ship %s has already moved")},
  {NULL, NULL}};

const char *cgettext(const char *token) {
  int i;
  for (i = 0; warnings[i].token; ++i) {
    int diff = strcmp(warnings[i].token, token);
    if (diff > 0) {
      break;
    }
    if (diff == 0) {
      return gettext(warnings[i].message);
    }
  }
  return token;
}

void warning_deprecated(const char *token, int line_no, char *order_buf,
                        int level) {
  warning(cgettext(token), line_no, order_buf, level);
}

void checkstring(char *s, size_t l, int type) {
  if (l > 0 && strlen(s) > l) {
    sprintf(warn_buf, _("Text too long (max. %d characters)"), (int)l);
    awarning(warn_buf, 2);
  }
  if (s[0] == 0 && type == NECESSARY)
    awarning(_("No text"), 1);

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

#define val(x) (x != 0)

unit *find_unit(int i, int t) {
  unit *u;

  for (u = units; u; u = u->next)
    if ((i < 0 && u->no < 0) || (u->no == i && val(u->temp) == val(t)))
      break;
  return u;
}

t_region *addregion(int x, int y, int pers) {
  t_region *r, *R;

  for (r = Regionen; r; r = r->next)
    if (x == r->x && y == r->y)
      break;

  if (r) {
    r->personen += pers;
  }
  else {
    r = (t_region *)calloc(1, sizeof(t_region));
    if (r) {
      r->x = x;
      r->y = y;
      r->personen = pers;
      r->geld = 0;
      r->reserviert = 0;
      r->name = STRDUP("Region");
      r->line_no = line_no;
      if (Regionen) {
        for (R = Regionen; R->next; R = R->next)
          ;          /* letzte Region der Liste  */
        R->next = r; /* Region hinten dranklemmen */
      }
      else
        Regionen = r;
    }
  }
  return r;
}

void addteach(unit *teacher, unit *student) {
  teach *t, **teachlist = &teachings;

  for (t = teachings; t; t = t->next) {
    if (t->student == student) {
      if (!teacher) /* Aufruf durch Schüler, aber Lehrer hat  schon Struktur
                       angelegt */
        return;
      if (t->teacher ==
          teacher) /* kann eigentlich nicht *  vorkommen, aber egal */
        return;
      if (t->teacher ==
          NULL) { /* Schüler hat Struct angelegt (mit  unbek. Lehrer), wir
                     tragen jetzt nur  noch den Lehrer nach */
        t->teacher = teacher;
        return;
      }
    }
  }
  t = (teach *)malloc(sizeof(teach));
  if (t) {
    t->next = NULL;
    t->teacher = teacher;
    t->student = student;
    addlist(teachlist, t);
  }
}

unit *newunit(int n, int t) {
  unit *u = find_unit(n, t), *c;

  if (t)
    current_temp_no = n;
  else
    current_temp_no = 0;

  if (!u) {
    u = (unit *)calloc(1, sizeof(unit));
    if (!u) return NULL;
    u->people = t ? 0 : 1;
    u->no = n;
    u->line_no = line_no;
    u->order = STRDUP(order_buf);
    u->region = addregion(Rx, Ry, 0);
    u->newx = Rx;
    u->newy = Ry;
    u->temp = t;
    if (units) {
      for (c = units; c->next; c = c->next)
        ;          /* letzte unit der Liste */
      c->next = u; /* unit hinten dranklemmen */
    }
    else
      units = u;
  }
  if (u->temp < 0) {
    log_error(filename, line_no, order_buf, this_unit_id(),
      _("TEMP %s is used in region %d, %d and region %d, %d (line %d)"),
      itob(u->no), Rx, Ry, u->newx, u->newy, u->start_of_orders_line);
    u->long_order_line = 0;
  }
  if (u->temp < 42)
    u->temp = t;
  return u;
}

int atoip(char *s) {
  int n;

  n = atoi(s);
  if (n < 0)
    n = 0;
  return n;
}

char *igetstr(char *s1) {
  int i;
  static char *s;
  static char buf[BUFSIZE];

  if (s1)
    s = s1;
  while (*s == SPACE)
    s++;

  for (i = 0; *s && *s != SPACE && i + 1 < (int)sizeof(buf); i++, s++) {
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

char *printkeyword(int key) {
  t_keyword *k = keywords;

  while (k && k->keyword != key) {
    k = k->next;
  }
  return (k && k->keyword == key) ? k->name : NULL;
}

char *printdirection(int dir) {
  t_direction *d = directions;

  while (d && d->dir != dir) {
    d = d->next;
  }
  return d ? d->name : 0;
}

char *printparam(int par) {
  t_params *p = parameters;

  while (p && p->param != par)
    p = p->next;
  return p ? p->name : 0;
}

char *printliste(int key, t_liste *L) {
  t_liste *l;

  for (l = L; key; l = l->next, key--)
    ;
  return l->name;
}

#define getstr() igetstr(NULL)
#define getb() btoi(igetstr(NULL))
#define geti() atoi(igetstr(NULL))
#define getI() btoi(igetstr(NULL))

int findstr(const char *v[], const char *s, int max) {
  int i;
  size_t ss = strlen(s);

  if (!s[0])
    return -1;
  for (i = 0; i < max; i++)
    if (v[i] && unicode_utf8_strncasecmp((utf8_t *)s, (utf8_t *)v[i], ss) == 0)
      return i;
  return -1;
}

int findtoken(const char *token, int type) {
  tnode *tk = &tokens[type];
  char buffer[1024];
  const char *str;

  if (!(*token))
    return -1;

  str = transliterate(buffer, sizeof(buffer), token);

  if (str) {
    if (type == UT_KEYWORD) {
      at_cmd = 0;
      bang_cmd = 0;
      for (; *str; ++str) {
        if (*str == '!')
          bang_cmd = 1;
        else if (*str == '@')
          at_cmd = 1;
        else
          break;
      }
      if ((at_cmd || bang_cmd) && *str < 65) {
        log_error(filename, line_no, order_buf, this_unit_id(), _("Space not allowed here"));
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

int findparam(const char *s) {
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

int isparam(char *s, int i, char print) {
  if (i != findparam(s))
    return 0;
  if (print) {
    Scat(printparam(i));
  }
  return 1;
}

t_spell *findspell(char *s) {
  t_spell *sp;

  if (!s[0] || !spells)
    return NULL;
  for (sp = spells; sp; sp = sp->next)
    if (sp->name && !unicode_utf8_strncasecmp((utf8_t *)sp->name, (utf8_t *)s, strlen(s)))
      return sp;
  return NULL;
}

t_skills *findskill(char *s) {
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
#define findreport(s) findstr(reports, s, MAXREPORTS)
#define findoption(s) findtoken(s, UT_OPTION)
#define findshiptype(s) findtoken(s, UT_SHIP);
#define getskill() findskill(getstr())
#define getparam() findparam(getstr())
#define igetparam(s) findparam(igetstr(s))

int finddirection(char *s) {
  if (!*s)
    return -2;
  if (strcmp(s, "//") ==
      0) /* "NACH NW NO NO // nach Xontormia" ist *   erlaubt */
    return -2;
  return findtoken(s, UT_DIRECTION);
}

#define getdirection() finddirection(getstr())

#define getoption() findoption(getstr())

#define findkeyword(s) findtoken(s, UT_KEYWORD)

char *getbuf(void) {
  char lbuf[MAXLINE];
  bool cont = false;
  bool quote = false;
  bool report = false;
  char *cp = warn_buf;

  lbuf[MAXLINE - 1] = '@';

  do {
    bool start = true;
    char *end;
    char *bp = fgetbuffer(lbuf, MAXLINE, F);

    if (!bp) {
      return NULL;
    }
    end = bp + strlen(bp);
    if (end == bp || *(end - 1) == '\n') {
      line_no++;
      *(--end) = 0;
    } else {
      /*
       * wenn die Zeile länger als erlaubt war, wird der Rest
       * weggeworfen:
       */
      while (bp && !lbuf[MAXLINE - 1] && lbuf[MAXLINE - 2] != '\n') {
        bp = fgetbuffer(warn_buf, 1024, F);
      }
      lbuf[MAXLINE - 1] = 0;
      log_warning(1, filename, line_no, lbuf, this_unit_id(), _("Line too long"));
      bp = lbuf;
    }
    cont = false;
    while (cp != warn_buf + MAXLINE && bp != lbuf + MAXLINE && *bp) {
      int c = *(unsigned char *)bp;
      char *skip = eatwhite(bp);
      if (skip != bp) {
        /* replace this whitespace with a single space */
        if (quote) {
          *(cp++) = SPACE_REPLACEMENT;
        } else if (!start) {
          *(cp++) = ' ';
        }
        bp = skip;
        continue;
      }
      cont = false;
      if (c == '"') {
        quote = !quote;
      } else {
        if (c == '\\')
          cont = true;
        else if (c < 0 || !iscntrl(c)) {
          *(cp++) = c;
        }
      }
      ++bp;
      start = false;
    }
    if (cp == warn_buf + MAXLINE) {
      --cp;
      if (!report) {
        report = true;
        log_error(filename, line_no, lbuf, this_unit_id(), _("Line too long"));
      }
    }
    if (cp) {
      *cp = 0;
    }
  } while (cont || cp == warn_buf);

  if (quote) {
    log_error(filename, line_no, lbuf, this_unit_id(), _("Missing \""));
  }
  return warn_buf;
}

void get_order(void) {
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

void getafaction(char *s) {
  int i;

  i = btoi(s);
  if (!s[0]) {
    log_error(filename, line_no, order_buf, this_unit_id(), _("Faction missing"));
  } else {
    if (!i)
      awarning(_("Faction 0 used"), 1);
    icat(i);
  }
}

int getmoreunits(bool partei) {
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
        log_error(filename, line_no, order_buf, this_unit_id(), _("Faction '%s' invalid"), s);
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
        log_error(filename, line_no, order_buf, this_unit_id(), _("Unit '%s' is not possible here"), s);
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

int getaunit(int type) {
  char *s, is_temp = 0;
  int i;

  current_temp_no = 0;
  cmd_unit = NULL;

  s = getstr();
  if (s[0] == 0) {
    if (type == NECESSARY) {
      log_error(filename, line_no, order_buf, this_unit_id(), _("Missing unit"));
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

  if (type ==
      42) { /* Nur Test, ob eine Einheit kommt, weil  das ein Fehler ist */
    if (i)
      return 42;
    return 0;
  }

  this_unit = i;

  cmd_unit = newunit(i, is_temp); /* Die Unit schon machen, wegen  TEMP-Check */
  bcat(i);
  if (is_temp)
    return 3;
  return 1;
}

void copy_unit(unit *from, unit *to) {
  to->no = from->no;
  to->people = from->people;
  to->money = from->money;
  to->line_no = from->line_no;
  to->temp = from->temp;
  to->lives = from->lives;
  to->start_of_orders_line = from->start_of_orders_line;
  to->long_order_line = from->long_order_line;
  if (from->start_of_orders_line)
    to->start_of_orders = STRDUP(from->start_of_orders);
  if (from->long_order_line)
    to->long_order = STRDUP(from->long_order);
  if (from->order)
    to->order = STRDUP(from->order);
  to->lernt = from->lernt;
}

void checkemail(void) {
  char *s, *addr;
  char bf[200];

  scat(printkeyword(K_EMAIL));
  addr = getstr();
  checkstring(addr, DESCRIBESIZE, NECESSARY);

  if (!addr) {
    awarning(_("Please set email with EMAIL"), 3);
    sprintf(bf, "; %s!", _("Please set email with EMAIL"));
    scat(bf);
    return;
  }
  s = strchr(addr, '@');
  if (!s) {
    log_error(filename, line_no, order_buf, this_unit_id(), _("invalid email address"));
    return;
  }
  scat(_("; Delivery to"));
  scat(addr);
}

/*
 * Check auf langen Befehl usw. - aus ACheck.c 4.1
 */

void end_unit_orders(void) {
  if (!order_unit) /* Für den ersten Befehl der ersten *  Einheit. */
    return;

  if (order_unit->lives > 0 && !order_unit->long_order_line &&
      order_unit->people > 0) {
    sprintf(warn_buf, _("Unit %s has no long order"), uid(order_unit));
    warning(warn_buf, order_unit->start_of_orders_line,
            order_unit->start_of_orders, 2);
  }
}

void orders_for_unit(int i, unit *u) {
  unit *t;
  char *k, *j, *e;
  int s;

  end_unit_orders();
  set_order_unit(mother_unit = u);

  if (u->start_of_orders_line) {
    sprintf(warn_buf, _("Unit %s already has a long order in line %d"), uid(u),
            u->start_of_orders_line);
    do {
      i++;
      if (i < 1)
        i = 1;
      u = newunit(i, 0);
    } while (u->start_of_orders_line);
    sprintf(warn_buf, _("Using unit %s"), itob(i));
    awarning(warn_buf, 1);
    set_order_unit(u);
  }

  u->start_of_orders = STRDUP(order_buf);
  u->start_of_orders_line = line_no;
  u->lives = 1;

  if (no_comment > 0)
    return;

  k = strchr(order_buf, '[');
  if (!k) {
    awarning(_("Cannot parse this unit's comment"), 4);
    no_comment++;
    return;
  }
  k++;
  while (!atoi(k)) { /* Hier ist eine [ im Namen; 0 Personen  ist nicht in der
                        Zugvorlage */
    k = strchr(k, '[');
    if (!k) {
      awarning(_("Cannot parse this unit's comment"), 4);
      no_comment++;
      return;
    }
    k++;
  }
  e = strchr(k, ']');
  u->people = atoi(k);
  j = strchr(k, ',');
  if (!j)
    j = strchr(k, ';');
  if (!j || j > e) {
    awarning(_("Cannot parse this unit's comment"), 4);
    no_comment++;
    return;
  }
  while (!isdigit(*j))
    j++;
  u->money += atoi(j);
  while (isdigit(*j))
    j++; /* hinter die Zahl */

  k = j;
  j = strchr(k, ',');
  if (!j)
    j = strchr(k, ';');

  if (j) {
    j++;
    if (j < e && *j == 'U') { /* Muß ein Gebäude unterhalten */
      j++;
      if (isdigit(*j)) {
        u->unterhalt = atoi(j);
        while (isdigit(*j))
          j++;
        j++;
      }
    }

    if (j < e && *j == 'I') { /* I wie Illusion, auch Untote - eben  alles, was
                                 nix frißt und keinen langen   Befehl braucht */
      u->lives = -1;
      j += 2; /* hinter das I und das Zeichen danach */
    }
    if (j < e && *j == 's') { /* Ist auf einem Schiff */
      j++;
      if (u->ship >= 0) /* hat sonst schon ein Schiffskommando! */
        u->ship = btoi(j);
    } else if (j < e && *j == 'S') { /* Ist Kapitän auf einem Schiff */
      j++;
      s = -btoi(j);
      for (t = units; t;
           t = t->next)     /* vielleicht hat schon * eine  Einheit durch */
        if (t->ship == s) { /* BETRETE das Kommando; das ist ja falsch  */
          t->ship = -s;
          break;
        }
      u->ship = s;
    }
  }
  addregion(Rx, Ry, u->people);
}

void orders_for_temp_unit(unit *u) {
  if (u->start_of_orders_line && u->region == Regionen) {
    /*
     * in Regionen steht die aktuelle Region drin
     */
    log_error(filename, line_no, order_buf, this_unit_id(), 
      _("TEMP %s was already used in line %d"),
      itob(u->no), u->start_of_orders_line);
    /*
     * return; Trotzdem kein return, damit die Befehle ordnungsgemäß
     * zugeordnet werden!
     */
  }
  u->line_no = line_no;
  u->lives = 1;
  if (u->order) {
    free(u->order);
  }
  u->order = STRDUP(order_buf);
  if (u->start_of_orders) {
    free(u->start_of_orders);
  }
  u->start_of_orders = STRDUP(order_buf);
  u->start_of_orders_line = line_no;
  mother_unit = order_unit;
  set_order_unit(u);
}

void long_order(void) {
  char *s, *q;
  int i;

  if (order_unit->long_order_line) {
    s = order_unit->long_order;
    q = strchr(s, ' ');
    if (q)
      *q = 0; /* Den Befehl extrahieren */
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
      return; /* FOLGE ist nur Trigger */

    if (strlen(order_unit->long_order) > DESCRIBESIZE + NAMESIZE)
      order_unit->long_order[DESCRIBESIZE + NAMESIZE] = 0;
    /*
     * zu lange Befehle kappen
     */
    sprintf(warn_buf, _("Unit %s already has a long order in line %d (%s)"),
            uid(order_unit), order_unit->long_order_line,
            order_unit->long_order);
    awarning(warn_buf, 1);
  } else {
    order_unit->long_order = STRDUP(order_buf);
    order_unit->long_order_line = line_no;
  }
}

void checknaming(void) {
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
    awarning(_("Names must not contain brackets"), 1);

  switch (i) {
  case -1:
    log_error(filename, line_no, order_buf, this_unit_id(), _("Unrecognized object"));
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
    log_error(filename, line_no, order_buf, this_unit_id(), _("This object cannot be renamed"));
  }
}

void checkdisplay(void) {
  int i;

  scat(printkeyword(K_DESCRIBE));
  i = findparam(getstr());

  switch (i) {
  case -1:
    log_error(filename, line_no, order_buf, this_unit_id(), _("Unrecognized object"));
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
    log_error(filename, line_no, order_buf, this_unit_id(), _("This object cannot be described"));
  }
}

void check_leave(void) {
  unit *t, *T = NULL;
  int s;

  s = order_unit->ship;
  order_unit->ship = 0;
  for (t = units; t; t = t->next)
    if (t->region == order_unit->region) {
      t->unterhalt += order_unit->unterhalt;
      strcpy(message_buf, uid(order_unit));
      sprintf(warn_buf,
              _("Moved maintainance for building from unit %s to unit %s"),
              message_buf, uid(t));
      break;
    }
  order_unit->unterhalt =
    0;         /* ACHTUNG! hierdurch geht die  Unterhaltsinfo verloren! */
  if (s < 0) { /* wir waren Kapitän, neuen suchen */
    for (t = units; t; t = t->next)
      if (t->ship == -s) /* eine Unit auf dem selben Schiff */
        T = t;           /* in ECheck sind die Units down->top,  darum */
    if (T)               /* die letzte der Liste==erste Unit im *  Programm */
      T->ship = s;
  }
}

void checkenter(void) {
  int i, n;
  unit *u;

  scat(printkeyword(K_ENTER));
  i = findparam(getstr());

  switch (i) {
  case -1:
    log_error(filename, line_no, order_buf, this_unit_id(), _("Unrecognized object"));
    return;
  case P_BUILDING:
  case P_CASTLE:
  case P_SHIP:
    Scat(printparam(i));
    n = getI();
    if (!n) {
      log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[OBJECTNUMBERMISSING]));
      return;
    } else
      bcat(n);
    break;
  default:
    log_error(filename, line_no, order_buf, this_unit_id(), _("This object cannot be entered"));
    return;
  }
  check_leave();
  if (i == P_SHIP) {
    order_unit->ship = n;
    for (u = units; u; u = u->next) /* ggf. Kommando geben */
      if (u->ship == -n)            /* da hat einer schon das Kommando */
        return;
    order_unit->ship = -n;
  }
}

int getaspell(char *s, char spell_typ, unit *u, int reallycast) {
  t_spell *sp;
  int p;

  if (*s == '[' || *s == '<') {
    log_error(filename, line_no, order_buf, this_unit_id(), _("Do not use [ ] and < >"));
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
        log_error(filename, line_no, order_buf, this_unit_id(), _("Error with region coordinates"));
        return 0;
      }
    } else {
      log_error(filename, line_no, order_buf, this_unit_id(), _("Error with REGION parameter"));
      return 0;
    }
    s = getstr();
  }
  if (findparam(s) == P_LEVEL) {
    scat(printparam(P_LEVEL));
    s = getstr();
    if (!*s || atoi(s) < 1) {
      log_error(filename, line_no, order_buf, this_unit_id(), _("Error with LEVEL parameter"));
      return 0;
    }
    p = atoi(s);
    icat(p);
    s = getstr();
  }
  sp = findspell(s);
  if (!sp) {
    if (u) {                 /* sonst ist das der Test von GIB */
      if (show_warnings > 0) /* nicht bei -w0 */
        log_error(filename, line_no, order_buf, this_unit_id(), _("Unrecognized spell"));
      if (*s >= '0' && *s <= '9') {
        log_error(filename, line_no, order_buf, this_unit_id(), _("%s or %s missing"),
          printparam(P_LEVEL), printparam(P_REGION));
      }
      qcat(s);
    }
    return 0;
  }
  qcat(sp->name);
  if (!(sp->typ & spell_typ)) {
    if (show_warnings > 0) {
      if (sp->typ & SP_ZAUBER) {
        log_error(filename, line_no, order_buf, this_unit_id(), _("%s is not a combat spell"), sp->name);
      }
      else {
        log_error(filename, line_no, order_buf, this_unit_id(), _("%s is a combat spell"), sp->name);
      }
    }
  } else {
    if (u && (sp->typ & SP_BATTLE) && (u->spell & sp->typ)) {
      sprintf(warn_buf, cgettext(Errors[UNITALREADYHAS]), uid(u));
      switch (sp->typ) {
      case SP_POST:
        strcat(warn_buf, cgettext(Errors[POST]));
        break;
      case SP_PRAE:
        strcat(warn_buf, cgettext(Errors[PRE]));
        break;
      }
      strcat(warn_buf, _("combat magic set"));
      if (show_warnings > 0) /* nicht bei -w0 */
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
  } while (*s); /* restliche Parameter ohne Check ausgeben  */
  return 1;
}

void checkgiving(void) {
  char *s;
  int i, n;

  if (from_temp_unit_no) {
    log_error(filename, line_no, order_buf, this_unit_id(), _("New units cannot GIVE anything"));
    return;
  }
  scat(printkeyword(K_GIVE));
  getaunit(NECESSARY);
  s = getstr();
  if (!getaspell(s, SP_ALL, NULL, 0) && !isparam(s, P_CONTROL, 1) &&
      !isparam(s, P_HERBS, 1) && !isparam(s, P_UNIT, 1)) {
    n = atoi(s);
    if (n < 1) {
      if (findparam(s) == P_ALLES) { /* GIB xx ALLES wasauchimmer */
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
          log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[NUMMISSING]));
          n = 1;
        }
      } else {
        log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[NUMMISSING]));
        n = 1;
      }
    }
    if (n > 0)
      icat(n);

    s = getstr();
    if (!(*s) && n < 0) { /* GIB xx ALLES */
      if (cmd_unit) {
        n = order_unit->money - order_unit->reserviert;
        cmd_unit->money += n;
        cmd_unit->reserviert += n;
      }
      order_unit->money = order_unit->reserviert;
      return;
    }

    if (!(*s)) {
      log_error(filename, line_no, order_buf, this_unit_id(), _("Give what?"));
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
        sprintf(warn_buf, Errors[UNITMISSPERSON], uid(order_unit));
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
        sprintf(warn_buf, Errors[UNITMISSSILVER], uid(order_unit));
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
            awarning(_("Unrecognized object"), 1);
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
        sprintf(warn_buf, _("Unit %s may lack control over ship %s"),
                uid(order_unit), itob(order_unit->ship));
        awarning(warn_buf, 4);
      } else if (cmd_unit) {
        if (cmd_unit->ship != 0 && abs(cmd_unit->ship) != -order_unit->ship) {
          sprintf(warn_buf, _("Unit %s may be on ship %s instead of ship %s"),
                  uid(cmd_unit), itob(-order_unit->ship),
                  itob(abs(cmd_unit->ship)));
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

void getluxuries(int cmd) {
  char *s;
  int n, i;

  s = getstr();

  n = atoi(s);
  if (n < 1) {
    if (findparam(s) == P_ALLES) {
      if (cmd == K_BUY) {
        log_error(filename, line_no, order_buf, this_unit_id(), _("BUY and ALL cannot be combined"));
        return;
      } else
        scat(printparam(P_ALLES));
    } else {
      log_error(filename, line_no, order_buf, this_unit_id(), _("Number of luxury items missing"));
    }
    n = 1;
  }

  icat(n);
  s = getstr();
  i = finditem(s);

  if (ItemPrice(i) < 1) {
    log_error(filename, line_no, order_buf, this_unit_id(), _("Not a luxury item"));
  }
  else {
    Scat(ItemName(i, n != 1));
    if (cmd == K_BUY) { /* Silber abziehen; nur Grundpreis! */
      i = ItemPrice(i);
      order_unit->money -= i * n;
      order_unit->reserviert -= i * n;
    }
  }
}

void checkmake(void) {
  int i, j = 0, k = 0;
  char *s;

  scat(printkeyword(K_MAKE));
  s = getstr();

  if (isdigit(*s)) { /* MACHE anzahl "Gegenstand" */
    j = atoi(s);
    if (j == 0)
      awarning(_("Number 0 does not make sense here"), 2);
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
      log_error(filename, line_no, order_buf, this_unit_id(), _("Number is not possible here"));
    next_indent = INDENT_NEW_ORDERS;
    scat(" TEMP");
    j = getb();
    if (!j)
      log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[NOTEMPNUMBER]));
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
      log_error(filename, line_no, order_buf, this_unit_id(), "%s %s <%s>",
        printkeyword(K_MAKE), printparam(P_ROAD), _("direction"));
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
    log_error(filename, line_no, order_buf, this_unit_id(), _("This cannot be made"));
  else
    dwarning(Errors[UNITMUSTBEONSHIP], 4);
  long_order();
  /*
   * es kam ja eine Meldung - evtl. kennt ECheck das nur nicht?
   */
}

void checkdirections(int key) {
  int i, count = 0, x, y, sx, sy, rx, ry;

  rx = sx = x = Rx;
  ry = sy = y = Ry;
  scat(printkeyword(key));

  do {
    i = getdirection();
    if (i <= -1) {
      if (i == -2)
        break; /* Zeile zu ende */
      log_error(filename, line_no, order_buf, this_unit_id(), _("Unrecognized direction"));
    } else {
      if (key == K_ROUTE && i == D_PAUSE && count == 0)
        awarning(_("ROUTE starts with PAUSE"), 2);
      if (key == K_MOVE && i == D_PAUSE) {
        log_error(filename, line_no, order_buf, this_unit_id(), _("%s and %s cannot be combined"),
                printkeyword(K_MOVE), printdirection(D_PAUSE));
        return;
      } else {
        Scat(printdirection(i));
        count++;
        if (!noship && order_unit->ship == 0 && key != K_ROUTE && count == 4) {
          sprintf(warn_buf, _("Unit %s may be moving too far"),
                  uid(order_unit));
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
            ry = y; /* ROUTE: ersten Abschnitt merken für  Silber verschieben */
          }
          break;
        }
      }
    }
  } while (i >= 0);

  if (!count)
    log_error(filename, line_no, order_buf, this_unit_id(), _("Unrecognized direction"));
  if (key == K_ROUTE && !noroute && (sx != x || sy != y)) {
    sprintf(warn_buf, cgettext(Errors[ROUTENOTCYCLIC]), sx, sy, x, y);
    awarning(warn_buf, 4);
  }
  if (!does_default) {
    if (order_unit->hasmoved) {
      log_error(filename, line_no, order_buf, this_unit_id(), 
        cgettext(Errors[UNITALREADYHASMOVED]), uid(order_unit));
      return;
    }
    order_unit->hasmoved = 2; /* 2: selber bewegt */
    if (key == K_ROUTE) {
      order_unit->newx = rx;
      order_unit->newy = ry;
    } else {
      order_unit->newx = x;
      order_unit->newy = y;
    }
  }
}

void check_sabotage(void) {
  if (getparam() != P_SHIP) {
    log_error(filename, line_no, order_buf, this_unit_id(), _("Only ships can be sabotaged"));
    return;
  }
  Scat(printparam(P_SHIP));
  return;
}

void checkmail(void) {
  char *s;

  scat(printkeyword(K_MESSAGE));
  s = getstr();
  if (STRICMP(s, "an") == 0 || STRICMP(s, "to") == 0)
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
    log_error(filename, line_no, order_buf, this_unit_id(), _("Messages must be sent to a %s, %s or %s"),
            printparam(P_UNIT), printparam(P_FACTION), printparam(P_REGION));
    break;
  }
  s = getstr();
  checkstring(s, 0, NECESSARY);
}

void claim(void) {
  char *s;
  int i, n;

  scat(printkeyword(K_CLAIM));
  s = getstr();
  n = atoi(s);
  if (n < 1) {
    log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[NUMMISSING]));
    n = 1;
  } else {
    s = getstr();
  }
  icat(n);
  if (!(*s)) {
    log_error(filename, line_no, order_buf, this_unit_id(), _("Unrecognized object"));
    return;
  }
  i = finditem(s);
  if (i <= 0) {
    awarning(_("Unrecognized object"), 1);
  }
  Scat(ItemName(i, n != 1));
}

void reserve(void) {
  char *s;
  int i, n;

  scat(printkeyword(K_RESERVE));

  if (from_temp_unit_no) {
    log_error(filename, line_no, order_buf, this_unit_id(), _("New units cannot use RESERVE, try using GIVE instead"));
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
        log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[NUMMISSING]));
        n = 1;
      }
    } else {
      log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[NUMMISSING]));
      n = 1;
    }
  }
  if (n > 0)
    icat(n);

  s = getstr();

  if (!(*s)) {
    log_error(filename, line_no, order_buf, this_unit_id(), _("RESERVE what?"));
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

void check_ally(void) {
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
  case P_NOT:
    Scat(printparam(i));
    break;
  default:
    log_error(filename, line_no, order_buf, this_unit_id(), _("Illegal argument for %s"), printkeyword(K_HELP));
    return;
  }

  s = getstr();
  if (findparam(s) == P_NOT) {
    Scat(printparam(P_NOT));
  }
}

int studycost(t_skills *talent) {
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
      if (!compile) {
        fprintf(ERR, cgettext(Errors[SCHOOLCHOSEN]), magiegebiet[i]);
      }
      i = geti();
    } else
      i = atoi(s);
    if (i < 100) {
      i = 200;
      awarning(_("Assuming learning costs of 200 silver"), 2);
    }
    return i;
  }
  return talent->kosten;
}

void check_comment(void) {
  char *s;
  int m;

  s = getstr();
  if (STRNICMP(s, "ECHECK", 6))
    return;
  s = getstr();

  if (STRNICMP(s, "VERSION", 7) == 0) {
    fprintf(ERR, "%s\n", order_buf);
    return;
  }
  if (STRNICMP(s, "NOWARN", 6) == 0) { /* Warnungen für nächste   Zeile aus */
    warn_off = 2;
    return;
  }
  if (STRNICMP(s, "LOHN", 4) == 0 ||
      STRNICMP(s, "WAGE", 4) == 0) { /* LOHN   für   Arbeit  */
    m = geti();
    lohn = (m > 10) ? m : 10;
    return;
  }
  if (STRNICMP(s, "ROUT", 4) == 0) { /* ROUTe */
    noroute = (char)(1 - noroute);
    return;
  }
  if (STRNICMP(s, "KOMM", 4) == 0 ||
      STRNICMP(s, "COMM", 4) == 0) { /* KOMMando  */
    m = geti();
    if (!m) {
      m = order_unit->ship;
      if (!m)
        m = rand();
    }
    order_unit->ship = -abs(m);
    return;
  }
  if (STRNICMP(s, "EMPTY", 5) == 0) {
    order_unit->money = 0;
    order_unit->reserviert = 0;
    return;
  }
  if (STRNICMP(s, "NACH", 4) == 0 || STRNICMP(s, "MOVE", 4) == 0) {
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

void check_money(
  bool do_move) { /* do_move=true: vor der Bewegung,  anschließend */
  unit *u, *t;    /* Bewegung ausführen, damit das Silber  bewegt wird */
  t_region *r;
  int i, x, y, um;

  u = find_unit(-1, 0);
  if (u) { /* Unit -1 leeren */
    u->people = u->money = u->unterhalt = u->reserviert = 0;
    u->lives = -1;
  }

  if (do_move) {
    for (u = units; u; u = u->next) { /* Check TEMP und leere Einheiten */
      if (u->lives < 1)               /* fremde Einheit oder Untot/Illusion */
        continue; /* auslassen, weil deren Geldbedarf nicht  zählt */

      if (u->temp && abs(u->temp) != 42) {
        log_error(filename, u->line_no, u->order, 0,
          _("Unit TEMP %s was never created with MAKE TEMP"), itob(u->no));
      }
      if (u->people < 0) {
        log_warning(3, filename, u->line_no, NULL, 0, 
          cgettext(Errors[UNITHASPERSONS]), uid(u), u->people);
      }

      if (u->people == 0 &&
          ((!nolost && !u->temp && u->money > 0) || u->temp)) {
        if (u->temp) {
          if (u->money > 0) {
            log_warning(2, filename, u->line_no, u->order, 0,
              cgettext(Errors[UNITHASNTPERSONS]), itob(u->no));
          }
          else {
            log_warning(2, filename, u->line_no, u->order, 0,
              _("Unit TEMP %s has no men and has not recruited any"),
              itob(u->no));
          }
        } else if (no_comment <= 0) {
          log_warning(3, filename, u->line_no, u->order, 0,
            _("Unit %s may lose silver and/or items"), itob(u->no));
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
            if (um > i)
              um = i;
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
        if (r->reserviert > 0 &&
            r->reserviert > r->geld) { /* nur  explizit   mit  RESERVIERE  */
            log_warning(3, filename, r->line_no, NULL, 0,
                 _("In %s (%d,%d) there was reserved more silver (%d) than "
                    "available (%d)."),
                  r->name, r->x, r->y, r->reserviert, r->geld);
        }
      }

    for (u = units; u;
         u = u->next) { /* fehlendes Silber aus dem  Silberpool nehmen */
      if (do_move && u->unterhalt) {
        u->money -= u->unterhalt;
        u->reserviert -= u->unterhalt;
      }
      if (u->money < 0 && silberpool) {
        for (t = units; t && u->money < 0; t = t->next) {
          if (t->region != u->region || t == u)
            continue;
          um = t->money - t->reserviert;
          if (um < -u->money)
            um = -u->money;
          if (um > 0) {
            u->money += um;
            u->reserviert +=
              um; /* das so erworbene Silber muß  auch reserviert sein */
            t->money -= um;
          }
        }
      }
      if (u->money < 0) {
        log_warning(3, filename, u->line_no, NULL, u->no,
          do_move ? _("Unit %s has %d silver before income")
          : _("Unit %s has %d silver"),
          uid(u), u->money);

        if (u->unterhalt) {
          if (do_move) {
            log_warning(1, filename, u->line_no, NULL, u->no,
              _("Unit %s needs %d more silver to maintain a building"),
              uid(u), -u->money);
          }
          else {
            log_warning(1, filename, u->line_no, NULL, u->no,
              _("Unit %s lacks %d silver to maintain a building"),
              uid(u), -u->money);
          }
        }
      }
    }
  }

  if (Regionen)
    for (r = Regionen; r; r = r->next)
      r->geld = 0; /* gleich wird Geld bei den Einheiten  bewegt, darum den Pool
                      leeren und nach  der Bewegung nochmal füllen */
  if (!do_move)    /* Ebenso wird es in check_living nochmal  neu eingezahlt */
    return;

  if (!Regionen) /* ohne Regionen Bewegungs-Check nicht *  sinnvoll */
    return;

  for (u = units; u; u = u->next) { /* Bewegen vormerken */
    if (u->lives < 1)               /* fremde Einheit oder Untot/Illusion */
      continue;
    if (u->hasmoved > 1) {
      if (!noship && u->ship > 0) {
        log_warning(4, filename, u->line_no, NULL, u->no,
          cgettext(Errors[UNITMOVESSHIP]), uid(u), itob(u->ship));
      }
      i = -u->ship;
      if (i > 0) { /* wir sind Kapitän; alle Einheiten auf  dem Schiff auch
                      bewegen */
        x = u->newx;
        y = u->newy;
        for (t = units; t; t = t->next) {
          if (t->ship == i) {
            if (t->hasmoved > 1) { /* schon bewegt! */
              log_warning(2, filename, u->line_no, NULL, u->no,
                cgettext(Errors[UNITONSHIPHASMOVED]), uid(t), itob(i));
            }
            t->hasmoved = 1;
            t->newx = x;
            t->newy = y;
          }
        }
      }
    }
  }

  for (u = units; u; u = u->next) { /* Bewegen ausführen */
    if (u->lives < 1)               /* fremde Einheit oder Untot/Illusion */
      continue;

    if (u->transport && u->drive && u->drive != u->transport) {
      sprintf(checked_buf, _("Unit %s is carried by unit %s but rides with %s"),
              uid(u), Uid(u->transport), Uid(u->drive));
      warning(checked_buf, u->line_no, u->long_order, 1);
      continue;
    }
    if (u->drive) { /* FAHRE; in u->transport steht die  transportierende
                       Einheit */
      if (u->hasmoved > 0) {
        log_error(filename, u->line_no, u->long_order, u->no,
          cgettext(Errors[UNITALREADYHASMOVED]), uid(u));
      }
      if (u->transport == 0) {
        t = find_unit(u->drive, 0);
        if (!t)
          t = find_unit(u->drive, 1);
        if (t && t->lives) {
          log_error(filename, u->line_no, u->long_order, u->no,
            _("Unit %s rides with unit %s, but the latter doesn't carry "
              "the former"),
            uid(u), Uid(u->drive));

        } else { /* unbekannte Einheit -> unbekanntes Ziel */
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
        log_error(filename, u->line_no, u->long_order, u->no,
          _("Unit %s carries unit %s, but the latter does not ride with "
            "the former"), Uid(u->transport), uid(u));
      }
    }

    if (u->hasmoved) { /* NACH, gefahren oder auf Schiff */
      addregion(u->region->x, u->region->y, -(u->people));
      u->region = addregion(u->newx, u->newy, u->people);
      if (u->region->line_no == line_no) /* line_no => NÄCHSTER */
        u->region->line_no = u->line_no;
    }
  }
}

void check_living(void) {
  unit *u;
  t_region *r;

  /*
   * Für die Nahrungsversorgung ist auch ohne Silberpool alles Silber
   * der Region zuständig
   */

  for (u = units; u;
       u = u->next) { /* Silber der Einheiten in den  Silberpool "einzahlen" */
    if (u->lives < 1) /* jetzt nach der Umverteilung von Silber */
      continue;
    u->region->geld +=
      u->money; /* jetzt wird reserviertes Silber  nicht festgehalten */
  }

  for (r = Regionen; r; r = r->next) {
    for (u = units; u; u = u->next)
      if (u->region == r && u->lives > 0)
        r->geld -= u->people * 10;
    if (r->geld < 0) {
      log_warning(4, filename, r->line_no, NULL, 0,
        _("There is not enough silver in %s (%d,%d) for upkeep; %d silver is missing."),
        r->name, r->x, r->y, -(r->geld));
    }
  }
}

void remove_temp(void) {
  /*
   * Markiert das TEMP-Flag von Einheiten der letzten Region -> falsche
   * TEMP-Nummern bei GIB oder so fallen auf
   */
  unit *u;

  for (u = units; u; u = u->next)
    u->temp = -u->temp;
}

void check_teachings(void) {
  teach *t;
  unit *u;
  int n;

  for (t = teachings; t; t = t->next) {
    t->student->schueler = t->student->people;
    if (t->teacher) {
      t->teacher->lehrer = t->teacher->people * 10;
      if (t->teacher->lehrer == 0) {
        log_warning(4, filename, t->student->line_no, NULL, t->student->no,
          _("Unit %s has 0 men and is taught by unit %s"),
          uid1(t->student), uid2(t->teacher));
      }
      if (t->student->schueler == 0 && t->student->lives > 0) {
        log_warning(4, filename, t->teacher->line_no, NULL, t->teacher->no,
          _("Unit %s has 0 men and teaches unit %s"),
          uid1(t->teacher), uid2(t->student));
      }
    }
  }

  for (t = teachings; t; t = t->next) {
    if (t->teacher == NULL || t->student->lives < 1) {
      /* lernt ohne  Lehrer bzw. */
      t->student->schueler = 0;  /* keine eigene Einheit */
      continue;
    }

    if (!t->student->lernt) {
      /* unbekannte TEMP-Einheit wird eh schon angemeckert */
      if (!t->student->temp) {
        log_warning(2, filename, t->student->line_no, NULL, t->student->no,
          _("Unit %s is taught by unit %s but doesn't learn"),
          uid1(t->student), uid2(t->teacher));
        t->student->schueler = 0;
      }
      continue;
    }

    n = (t->teacher->lehrer < t->student->schueler) ? t->teacher->lehrer
                                                    : t->student->schueler;
    t->teacher->lehrer -= n;
    t->student->schueler -= n;
  }

  for (u = units; u; u = u->next) {
    if (u->lives < 1)
      continue;
    if (u->lehrer > 0) {
      log_warning(5, filename, u->line_no, NULL, u->no,
        _("Unit %s can teach %d more students"), uid(u), u->lehrer);
    }
    if (u->schueler > 0) {
      log_warning(5, filename, u->line_no, NULL, u->no,
        _("Unit %s Unit could benefit from %d more teachers"),
        uid(u), (u->schueler + 9) / 10);
    }
  }
}

void checkanorder(char *Orders) {
  int i, x;
  char *s;
  t_skills *sk;
  unit *u;

  s = strchr(Orders, ';');
  if (s)
    *s = 0; /* Ggf. Kommentar kappen */

  if (Orders[0] == 0)
    return; /* Dies war eine Kommentarzeile */

  if (Orders != order_buf) {
    /*
     * strcpy für überlappende strings - auch für gleiche - ist
     * undefiniert
     */
    strcpy(order_buf, Orders);
  }

  i = igetkeyword(order_buf);
  this_command = i;
  switch (i) {
  case K_NUMBER:
    scat(printkeyword(K_NUMBER));
    i = getparam();
    if (!(i == P_UNIT || i == P_SHIP || i == P_BUILDING || i == P_CASTLE ||
          i == P_FACTION)) {
      log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[ORDERNUMBER]));
      break;
    }
    Scat(printparam(i));
    if (getaunit(POSSIBLE) != 1) /* "TEMP xx" oder "0" geht nicht */
      log_error(filename, line_no, order_buf, this_unit_id(), _("Wrong number"));
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
      } else {
        log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[NEEDBOTHCOORDINATES]));
      }
    } else {
      log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[NEEDBOTHCOORDINATES]));
    }
    break;

  case K_USE:
    scat(printkeyword(K_USE));
    s = getstr();

    if (isdigit(*s)) { /* BENUTZE anzahl "Trank" */
      i = atoi(s);
      icat(i);
      if (i == 0)
        awarning(_("Number 0 does not make sense here"), 2);
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
        log_error(filename, line_no, order_buf, this_unit_id(), _("Unrecognized object"));
      } else {
        awarning(_("Unrecognized object"), 1);
      }
    }
    /* additional parameters may be in order */
    do {
      s = getstr();
      if (*s)
        Scat(s);
    } while (*s);

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
    if (getaunit(NECESSARY) == 3) {
      log_error(filename, line_no, order_buf, this_unit_id(), _("Cannot attack a newly formed unit"));
    }
    if (!attack_warning) {
      /*
       * damit laengere Angriffe nicht in Warnungs-Tiraden ausarten
       */
      dwarning(_("Longer combats exclude long orders"), 5);
      attack_warning = 1;
    }
    if (getaunit(42) == 42) {
      log_error(filename, line_no, order_buf, this_unit_id(), 
           _("There must be one ATTACK-order per unit"));
    }
    break;

  case K_SIEGE:
    scat(printkeyword(K_SIEGE));
    i = getI();
    if (!i)
      log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[NUMCASTLEMISSING]));
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
    if (isdigit(*s)) { /* ZÜCHTE anzahl KRÄUTER */
      x = atoi(s);
      if (x <= 0)
        awarning(_("Positive value expected"), 2);
      s = getstr();
    }

    i = findparam(s);
    if (x > 0 && i != P_HERBS)
      log_error(filename, line_no, order_buf, this_unit_id(), _("Number is not possible here"));

    if (i == P_HERBS || i == P_HORSE) {
      /* ZÜCHTE [anzahl] KRÄUTER */
      /* ZÜCHTE PFERDE */
      Scat(printparam(i));
    } else if (s && (*s)) {
      log_error(filename, line_no, order_buf, this_unit_id(), _("Illegal argument for %s"), printkeyword(K_BREED));
    } else {
      log_error(filename, line_no, order_buf, this_unit_id(), _("Missing argument for %s"), printkeyword(K_BREED));
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
      log_error(filename, line_no, order_buf, this_unit_id(), _("Wrong parameter"));
    }
    break;

  case K_PAY:
    scat(printkeyword(K_PAY));
    s = getstr();
    if (findparam(s) == P_NOT) {
      Scat(printparam(P_NOT));
    }
    break;

  case K_ALLIANCE:
    Scat(order_buf);
    break;

  case K_END:
    if (from_temp_unit_no == 0) {
      awarning(_("No matching MAKE TEMP for END"), 2);
    }
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
      Scat(printparam(P_HERBS)); /* momentan nur FORSCHE KRÄUTER */
    } else {
      log_error(filename, line_no, order_buf, this_unit_id(), _("Only herbs can be researched"));
    }
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
          icat(i);
        } else
          log_error(filename, line_no, order_buf, this_unit_id(), _("Wrong parameter"));
      }
      break;
    }
    if (isdigit(s[0])) {
      i = atoip(s);
      icat(i);
      break;
    }
    sprintf(warn_buf, _("%s needs parameters"), printkeyword(K_SETSTEALTH));
    awarning(warn_buf, 5);
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
              log_error(filename, line_no, order_buf, this_unit_id(), _("Wrong fight state"));
          }
        } else
          log_error(filename, line_no, order_buf, this_unit_id(), _("Wrong fight state"));
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
      log_error(filename, line_no, order_buf, this_unit_id(), _("Wrong parameter"));
    }

    break;

  case K_SPY:
    scat(printkeyword(K_SPY));
    i = getb();
    if (!i)
      log_error(filename, line_no, order_buf, this_unit_id(), _("Missing unit"));
    else
      bcat(i);
    long_order();
    break;

  case K_TEACH:
    scat(printkeyword(K_TEACH));
    if (!getmoreunits(false))
      log_error(filename, line_no, order_buf, this_unit_id(), _("Teacher needs students"));
    long_order();
    break;

  case K_FORGET:
    scat(printkeyword(K_FORGET));
    sk = getskill();
    if (!sk)
      log_error(filename, line_no, order_buf, this_unit_id(), _("Unrecognized skill"));
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
      } else {
        sk = findskill(s);
      }
    }
    if (!sk) {
      log_error(filename, line_no, order_buf, this_unit_id(), _("Unrecognized skill"));
    } else {
      Scat(sk->name);
      if (sk->kosten < 0) {
        if (order_unit->people > 1) {
          log_error(filename, line_no, order_buf, this_unit_id(), _("Magicians may have only one person"));
        }
      }
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
      dwarning(Errors[PASSWORDCLEARED], 0);
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
      u->people = i;
      addregion(Rx, Ry, i);
    } else
      log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[MISSINGNUMRECRUITS]));
    break;

  case K_QUIT:
    scat(printkeyword(K_QUIT));
    s = getstr();
    if (!s[0])
      log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[MISSINGPASSWORD]));
    else
      checkstring(s, NAMESIZE, POSSIBLE);
    dwarning(Errors[QUITMSG], 0);
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
    i = geti();
    if (!does_default) {
      if (!i)
        i = 20 * order_unit->people;
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
        log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[FOLLOW]));
    }
    break;

  case K_REPORT:
    scat(printkeyword(K_REPORT));
    s = getstr();
    i = findreport(s);
    if (i == -1) {
      if (unicode_utf8_strncasecmp((utf8_t *)s, (utf8_t *)printkeyword(K_SHOW), strlen(s)))
        log_error(filename, line_no, order_buf, this_unit_id(), _("Unrecognized report option"));
      else {
        Scat(printkeyword(K_SHOW));
      }
      break;
    }
    Scat(reports[i]);
    s = getstr();
    i = findstr(message_levels, s, ML_MAX);
    if (i == -1) {
      log_error(filename, line_no, order_buf, this_unit_id(), _("Wrong output level"));
      break;
    }
    Scat(message_levels[i]);
    break;

  case K_OPTION:
    scat(printkeyword(K_OPTION));
    i = getoption();
    if (i < 0) {
      log_error(filename, line_no, order_buf, this_unit_id(), _("Unrecognized option"));
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
    scat(printkeyword(K_DESTROY));
    break;

  case K_RIDE:
    scat(printkeyword(K_RIDE));
    if (getaunit(NECESSARY) == 2)
      log_error(filename, line_no, order_buf, this_unit_id(), _("Unit 0/Peasants not possible here"));
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
        dwarning(Errors[NOCARRIER], 3);
    }
    if (getaunit(42) == 42)
      log_error(filename, line_no, order_buf, this_unit_id(), 
          _("There must be one CARRY-order per unit"));
    break;

  case K_PIRACY:
    getmoreunits(true);
    long_order();
    break;

  case K_SCHOOL: /* Magiegebiet */
    s = getstr();
    i = findstr(magiegebiet, s, 5);
    if (i < 0) {
      log_error(filename, line_no, order_buf, this_unit_id(), _("School '%s' does not exist"), s);
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
    while (getstr()[0])
      ;
    copy_unit(u, order_unit);
    u->no = -1;
    u->long_order_line = 0;
    u->start_of_orders_line = 0;
    u->temp = 0;
    break;

  case K_COMMENT:
    check_comment();
    scat(Orders);
    break;

  case K_RESERVE:
    reserve();
    break;

  case K_CLAIM:
    claim();
    break;

  case K_GROUP:
    s = getstr();
    if (*s)
      Scat(s);
    break;

  case K_SORT:
    scat(printkeyword(K_SORT));
    s = getstr();
    if (*s) {
      if (unicode_utf8_strncasecmp((utf8_t *)s, (utf8_t *)printparam(P_BEFORE), strlen(s)) == 0 ||
          unicode_utf8_strncasecmp((utf8_t *)s, (utf8_t *)printparam(P_AFTER), strlen(s)) == 0) {
        Scat(s);
        i = getaunit(NECESSARY);
        if (i == 1 || i == 3) /* normale oder TEMP-Einheit: ok */
          break;
      }
    }
    log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[SORT]));
    break;

  case K_PLANT: {
    int n = 1;
    scat(printkeyword(K_PLANT));
    s = getstr();
    if (isdigit(*s)) { /* PFLANZE anzahl "Kraeuter/Samen/Mallornsamen" */
      n = atoi(s);
      if (n == 0) {
        awarning(_("Number 0 does not make sense here"), 2);
      }
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
        log_error(filename, line_no, order_buf, this_unit_id(), _("Unrecognized object"));
      } else {
        awarning(_("Unrecognized object"), 1);
      }
    }
    long_order();
  } break;

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
    else {
      sprintf(warn_buf, _("Missing argument for %s"), printkeyword(i));
      log_error(filename, line_no, order_buf, this_unit_id(), warn_buf);
    }
    break;
  default:
    log_error(filename, line_no, order_buf, this_unit_id(), _("Unrecognized order"));
  }
  if (does_default != 1) {
    porder();
    does_default = 0;
  }
}

void readaunit(void) {
  int i;
  unit *u;

  i = getb();
  if (i == 0) {
    log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[MISSINGUNITNUMBER]));
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
    if (i < -1)
      continue; /* Fehler: "@ Befehl" statt "@Befehl" */
    if (i < 0) {
      if (order_buf[0] == ';') {
        check_comment();
        continue;
      } else
        switch (igetparam(order_buf)) {
        case P_UNIT:
        case P_FACTION:
        case P_NEXT:
        case P_REGION:
          if (from_temp_unit_no != 0) {
            sprintf(warn_buf, _("%s %s lacks closing %s"), printparam(P_TEMP),
                    itob(from_temp_unit_no), printkeyword(K_END));
            awarning(warn_buf, 2);
            from_temp_unit_no = 0;
          }
          return;
        }
    }
    if (order_buf[0])
      checkanorder(order_buf);
  }
}

int readafaction(void) {
  int i;
  char *s;

  if (line_start == 1)
    line_no = 1;
  i = getI();
  if (i > 0) {
    bcat(i);
    s = getstr();
    if (s[0] == 0 || strcmp(s, "hier_passwort_eintragen") == 0) {
      if (compile) /* nicht uebertreiben im Compiler-Mode */
        log_error(filename, line_no, order_buf, this_unit_id(), _("Incorrect password"));
      else
        fputs(cgettext(Errors[PASSWORDMSG2]), ERR);
    } else
      qcat(s);
  } else
    log_error(filename, line_no, order_buf, this_unit_id(), cgettext(Errors[MISSINGFACTIONNUMBER]));

  indent = next_indent = INDENT_FACTION;
  porder();
  return i;
}

void help_keys(char key) {
  int i;

  switch (key) {
  case 'b': /* Befehle */
  case 'c': /* Commands */
    fprintf(ERR, "Befehle / commands:\n\n");
    for (i = 1; i < MAXKEYWORDS; i++)
      fprintf(ERR, "%s\n", Keywords[i]);
    break;

  case 's': /* Schlüsselworte */
  case 'k': /* Keywords */
    fprintf(ERR, "Schlüsselworte / keywords:\n\n");
    for (i = 1; i < UT_MAX; i++)
      fprintf(ERR, "%s\n", Keys[i]);
    break;

  case 'p': /* Parameter */
    fprintf(ERR, "Parameter / parameters:\n\n");
    for (i = 0; i < MAXPARAMS; i++)
      fprintf(ERR, "%s\n", Params[i]);
    break;

  case 'r': /* Richtungen */
  case 'd': /* Directions */
    fprintf(ERR, "Richtungen / directions:\n\n");
    for (i = 0; i < MAXDIRECTIONS; i++)
      fprintf(ERR, "%s\n", Directions[i]);
    break;

  case 'm':
  case 'f':
    break;
  default:
    return;
  }
  exit(0);
}

void files_not_found(FILE *F) {
  fputs("\n  **  ", F);
  fprintf(F, "ECheck V%s, %s", echeck_version, __DATE__);
  fputs("   **\n\n", F);
  fprintf(F, _("cannot read configuration files from %s/%s"), g_basedir,
          echeck_locale);
  fputs("\n\n", F);
}

void printversion() {
  fprintf(ERR,
          _("ECheck (Version %s, %s), order file checker for Eressea - "
            "freeware!"),
          echeck_version, __DATE__);
  fputc('\n', ERR);
}

void printhelp() {
  printversion();
  fputc('\n', ERR);
  fprintf(ERR,
          _("-Ppath  search path for the additional files;"
            " locale %s will be appended\n"
            "-Rgame  read files from subdirectory game; default: e2\n"
            "-Lloc   select locale loc\n"
            "-       use stdin instead of an input file\n"
            "-b      suppress warnings and errors (brief)\n"
            "-c      compiler-like output\n"
            "-m      magellan-useable output\n"
            "-e      send checked file to stdout, errors to stderr\n"
            "-E      send checked file to stdout, errors to stdout\n"
            "-s      use stderr for warnings, errors, etc. instead of stdout\n"
            "-ofile  write checked file into 'file'\n"
            "-Ofile  write errors into 'file'\n"
            "-p      abbreviate some output for piping\n"
            "-q      do not expect hints regarding men/silver within [] after "
            "UNIT\n"
            "-rnnn   set recruit costs to nnn silver\n"
            "-l      simulate silverpool\n"
            "-x      line counting starts with FACTION\n"
            "-n      do not count lines with NameMe comments (;;)\n"
            "-noxxx  no xxx warnings. xx can be:\n"
            "  ship   unit steers a ship but may lack control\n"
            "  route  do not check for cyclic ROUTE\n"
            "  lost   unit loses silver and items\n"
            "-w[n]   warnings of level n (default: 4)\n"
            "        1: mainly syntax errors\n"
            "        4: almost all errors\n"
            "        5: teachers / students\n"
            "-Q      no effect\n"
            "-C      no effect\n"
            "-vm.l   no effect\n"
            "-h      show this little help and exit\n"
            "-V      print version information and exit"),
          echeck_locale);
  fputc('\n', ERR);
}

enum {
  ACT_DEFAULT,
  ACT_VERSION,
  ACT_HELP,
  ACT_ABORT,
  ACT_ERROR
};

static int action = ACT_DEFAULT;

static void set_action(int new_action) {
  if (new_action > action)
    action = new_action;
}


int check_options(int argc, char *argv[], char dostop, char command_line) {
  int i;
  char *x;

  for (i = 1; i < argc && action < ACT_ABORT; i++) {
    if (argv[i][0] == '-') {
      switch (argv[i][1]) {
      case 'P':
        if (dostop) { /* bei Optionen via "; ECHECK" nicht mehr  machen */
          if (argv[i][2] == 0) { /* -P path */
            i++;
            if (argv[i]) {
              g_basedir = argv[i];
            } else {
              fputs(_("Empty path invalid"), stderr);
              fputc('\n', stderr);
              set_action(ACT_ERROR);
            }
          } else if (*(argv[i] + 2)) {
            /* -Ppath */
            g_basedir = argv[i] + 2;
          }
        }
        break;

      case 'Q':
        /* deprecated, was: non-verbose output */
        break;

      case 'C':
        /* deprecated, was: compact output */
        break;

      case 'v':
        /* deprecated */
        if (argv[i][2] == 0) { /* -V version */
          i++;
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
        if (argv[i][2] == 0) { /* -r nnn */
          i++;
          if (argv[i])
            rec_cost = atoi(argv[i]);
          else {
            fprintf(stderr,
                    _("Missing recruiting costs, set to %d"), rec_cost);
            fputc('\n', stderr);
          }
        } else
          rec_cost = atoi(argv[i] + 2);
        break;

      case 'c':
        compile = OUT_COMPILE;
        break;

      case 'm':
        compile = OUT_MAGELLAN;
        break;

      case 'E':
        if (dostop) { /* bei Optionen via "; ECHECK" nicht mehr  machen */
          echo_it = 1;
          OUT = stdout;
          ERR = stdout;
        }
        break;

      case 'e':
        if (dostop) { /* bei Optionen via "; ECHECK" nicht mehr  machen */
          echo_it = 1;
          OUT = stdout;
          ERR = stderr;
        }
        break;

      case 'O':
        if (dostop) { /* bei Optionen via "; ECHECK" nicht mehr  machen */
          if (argv[i][2] == 0) { /* "-O file" */
            i++;
            x = argv[i];
          } else /* "-Ofile" */
            x = argv[i] + 2;
          if (!x) {
            fputs(_("Using stderr for error output"), stderr);
            fputc('\n', stderr);
            ERR = stderr;
            break;
          }
          ERR = fopen(x, "w");
          if (!ERR) {
            fprintf(stderr,
                    _("Can't write to file `%s':\n"
                    " %s"), x, strerror(errno));
            fputc('\n', stderr);
            set_action(ACT_ABORT);
          }
        }
        break;

      case 'o':
        if (dostop) { /* bei Optionen via "; ECHECK" nicht mehr  machen */
          if (argv[i][2] == 0) { /* "-o file" */
            i++;
            x = argv[i];
          } else /* "-ofile" */
            x = argv[i] + 2;
          echo_it = 1;
          if (!x) {
            fputs(_("No name given for checked file, using stdout"), stderr);
            fputc('\n', stderr);
            OUT = stdout;
            break;
          }
          OUT = fopen(x, "w");
          if (!OUT) {
            fprintf(stderr, _("Can't write to file `%s':\n"
                    " %s"), x, strerror(errno));
            fputc('\n', stderr);
            action = ACT_ABORT;
          }
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
        if (dostop) /* bei Optionen via "; ECHECK" nicht mehr  machen */
          ERR = stderr;
        break;

      case 'n':
        if (strlen(argv[i]) > 2) {
          if (argv[i][3] == 0) { /* -no xxx */
            i++;
            x = argv[i];
          } else
            x = argv[i] + 3;
          if (!x) {
            fputs(_("-no what?"), stderr);
            fputc('\n', stderr);
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
          if (dostop) { /* bei Optionen via "; ECHECK" nicht mehr  machen */
            set_action(ACT_HELP);
          }
        } else {
          fprintf(ERR, _("Unknown option %s"), argv[i]);
          fputc('\n', stderr);
          if (dostop) { /* Nicht stoppen, wenn dies die Parameter  aus der Datei
                         selbst sind! */
            set_action(ACT_ERROR);
          }
        }
        break;

      case '?':
      case 'h':
        if (dostop) {
          /* bei Optionen via "; ECHECK" nicht mehr  machen */
          set_action(ACT_HELP);
        }
        break;
      case 'R': /* -R rules */
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
        if (dostop) {
          set_action(ACT_VERSION);
        }
        break;
      case 'L':
        if (argv[i][2] == 0) { /* -L loc */
          i++;
          if (argv[i])
            echeck_locale = argv[i];
          else {
            fprintf(stderr, _("Missing locale, using '%s'"), "de");
            fputc('\n', stderr);
          }
        } else {
          echeck_locale = argv[i] + 2;
        }
        break;

      default:
        if (argv[i][1]) { /* sonst ist das nur '-' => stdin lesen */
          fprintf(ERR, _("Unknown option %s"), argv[i]);
          fputc('\n', ERR);
          if (dostop) {
            /* Nicht stoppen, wenn dies die Parameter aus der Datei selbst sind!
             */
            set_action(ACT_ABORT);
          }
        }
      }
    } else
      break;
  }

  switch (action) {
  case ACT_HELP:
    printhelp();
    exit(0);
    break;
  case ACT_VERSION:
    printversion();
    exit(0);
    break;
  case ACT_ABORT:
    exit(0);
    break;
  case ACT_ERROR:
    exit(-1);
    break;
  case ACT_DEFAULT:
    break;
  default:
    fputs("Internal Error", stderr);
    break;
  }

  return i;
}

void parse_options(char *p, char dostop) {
  char *argv[10], **ap = argv, *vl, argc = 0;

  vl = strtok(p, " \t, ");
  do {
    *ap++ = vl;
    argc++;
  } while ((vl = strtok(NULL, " \t, ")) != NULL);
  *ap = 0;
  check_options(argc, argv, dostop, 0);
}

void check_OPTION(void) {
  get_order();
  if (befehle_ende)
    return;
  if (strncmp(order_buf, "From ", 5) == 0) { /* es ist eine Mail */
    do { /* Bis zur Leerzeile suchen -> Mailheader  zu Ende */
      fgetbuffer(order_buf, BUFSIZE, F);
    } while (order_buf[0] != '\n' && !feof(F));
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
        if (STRNICMP(order_buf, "; OPTION", 8) == 0 ||
            STRNICMP(order_buf, "; ECHECK", 8) == 0) {
          parse_options((char *)(order_buf + 2), 0);
          /*
           * "; " überspringen; zeigt dann auf "OPTION"
           */
        } else if (STRNICMP(order_buf, "; VERSION", 9) == 0)
          fprintf(ERR, "%s\n", order_buf);
      }
      get_order();
      if (befehle_ende)
        return;
    } while (order_buf[0] == COMMENT_CHAR);
}

void process_order_file(int *faction_count, int *unit_count) {
  int f = 0, next = 0;
  t_region *r;
  teach *t;
  unit *u;
  char *x;

  line_no = befehle_ende = 0;

  check_OPTION();

  if (befehle_ende) /* dies war wohl eine Datei ohne Befehle */
    return;

  Rx = Ry = -10000;

  /*
   * Auffinden der ersten Partei, und danach abarbeiten bis zur letzten
   * Partei
   */

  while (!befehle_ende) {
    switch (igetparam(order_buf)) {
    case P_LOCALE:
      x = getstr();
      get_order();
      break;

    case P_REGION:
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
              x = strchr(++x, ' '); /* 2. Space ist Trenner? */
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
        awarning(_("Error with REGION"), 0);
      r = addregion(Rx, Ry, 0);
      r->line_no = line_no;
      x = strchr(order_buf, ';');
      if (x) {
        x++;
        x = eatwhite(x);
        if (r->name) {
          free(r->name);
        }
        r->name = STRDUP(x);
        x = strchr(r->name, '\n');
        if (x)
          *x = 0;
      } else {
        if (r->name)
          free(r->name);
        r->name = STRDUP("");
      }
      get_order();
      break;

    case P_FACTION:
      if (f && !next) {
        log_error(filename, line_no, order_buf, this_unit_id(), _("Missing %s"), printparam(P_NEXT));
      }
      scat(printparam(P_FACTION));
      befehle_ende = 0;
      f = readafaction();
      if (brief <= 1) {
        if (!compile) {
          fprintf(ERR, _("Found orders for faction %s."), itob(f));
        }
        fputc('\n', ERR);
      }
      check_OPTION(); /* Nach PARTEI auf "; OPTION" bzw. ";  ECHECK" testen */
      if (faction_count)
        ++*faction_count;
      if (befehle_ende)
        return;
      if (!compile) {
        fprintf(
          ERR,
          _("Recruit costs have been set to %d silver, warning level %d"),
          rec_cost, show_warnings);
        fputc('\n', ERR);
        if (silberpool) {
          fputs(_("Silver pool is active."), ERR);
        }
        fputs("\n\n", ERR);
      }
      next = 0;
      break;

    case P_UNIT:
      if (f) {
        scat(order_buf);
        readaunit();
        if (unit_count)
          ++*unit_count;
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

    case P_NEXT:
      f = 0;
      scat(printparam(P_NEXT));
      indent = next_indent = INDENT_FACTION;
      porder();
      next = 1;

      check_money(true); /* Check für Lerngeld, Handel usw.; true:   dann
                            Bewegung ausführen */
      if (Regionen) {
        check_money(
          false);       /* Silber nochmal in den Pool, fehlendes  aus Pool */
        check_living(); /* Ernährung mit allem Silber der Region */
      }
      check_teachings();
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

    default:
      if (order_buf[0] == ';') {
        check_comment();
      } else {
        if (f && order_buf[0])
          awarning(_("Not carried out by any unit"), 1);
      }
      get_order();
    }
  } /* end while !befehle_ende */

  if (igetparam(order_buf) ==
      P_NEXT) /* diese Zeile wurde ggf. gelesen  und dann kam */
    next = 1; /* EOF -> kein Check mehr, next=0... */
  if (f && !next) {
    log_error(filename, line_no, order_buf, this_unit_id(), _("Missing %s"), printparam(P_NEXT));
  }
}

void addtoken(tnode *root, const char *str, int id) {
  static struct replace {
    char c;
    char *str;
  } replace[] = {{0xe4, "ae"}, {0xc4, "ae"}, {0xf6, "oe"}, {0xd6, "oe"},
                 {0xfc, "ue"}, {0xdc, "ue"}, {0xdf, "ss"}, {0, 0}};
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
    while (tk && tk->c != c) {
      tk = tk->nexthash;
    }
    if (!tk) {
      tk = (tnode *)calloc(1, sizeof(tnode));
      if (tk) {
        tk->id = -1;
        tk->c = c;
        tk->nexthash = root->next[lindex];
        root->next[lindex] = tk;
      }
    }
    if (tk) {
      addtoken(tk, str + 1, id);
    }
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

void inittokens(void) {
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

int readfiles(void) { /* liest externen Files */
  int i;

  if (!g_basedir) {
    return 0;
  }
  /*
   * alle Files aus der Liste der Reihe nach zu lesen versuchen
   */
  for (i = 0; i < filecount; i++) {
    readafile(ECheck_Files[i].name, ECheck_Files[i].type);
  }
  if (filesread == HAS_ALL) {
    inittokens();
  }
  return filesread;
}

int fileexists(const char *name) {
  struct STAT stats;
  return STAT(name, &stats);
}

const char *findfiles(const char *dir) {
  char zPath[FILENAME_MAX];
  SNPRINTF(zPath, sizeof(zPath), "%s/%s/%s/%s", dir, echeck_rules,
           echeck_locale, ECheck_Files[0].name);
  if (0 == fileexists(zPath)) {
    return dir;
  }
  return NULL;
}

static const char *findpath(void) {
  int i;
#ifndef WIN32
  const char *hints[] = {
    "/usr/share/games/echeck",
    "/usr/local/share/echeck",
    "/usr/share/echeck",
    NULL
  };
  const char *home = getenv("HOME");
  if (home) {
    snprintf(checked_buf, sizeof(checked_buf), "%s/.echeck", home);
    if (findfiles(checked_buf)) {
      return g_path = STRDUP(checked_buf);
    }
  }

  for (i = 0; hints[i]; ++i) {
    if (findfiles(hints[i])) {
      return hints[i];
    }
  }
#endif
  /* finally, check the binary's location, because windows installs 
   * everything in one place under program files.
   */
  i = wai_getExecutablePath(NULL, 0, NULL);
  g_path = malloc(i);
  if (g_path) {
    wai_getExecutablePath(g_path, i, &i);

    if (i > 0) {
      g_path[i] = '\0';
      if (findfiles(g_path)) {
        return g_path;
      }
      free(g_path);
      g_path = NULL;
    }
  }
  return NULL;
}

void echeck_init(void) {
  int i;
  for (i = 0; i < MAX_ERRORS; i++) {
    Errors[i] =
      STRDUP(Errors[i]); /* mit Defaults besetzten, weil NULL ->  crash */
  }
  /*
   * Path-Handling
   */
  filename = getenv("ECHECKOPTS");
  g_basedir = getenv("ECHECKPATH");
  if (!g_basedir) {
    /* for easy development, check current working directory first. */
    if (findfiles(".")) {
      g_basedir = ".";
    }
    else {
      /* check os-specific locations */
      g_basedir = findpath();
    }
  }
}

#ifdef HAVE_GETTEXT
#ifdef WIN32
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif
void init_intl(void) {
  char *path = NULL;
  const char *reldir = "locale";
  size_t length;
  int dirname_length;

  setlocale(LC_ALL, "");

  length = (size_t)wai_getExecutablePath(NULL, 0, &dirname_length);
  if (length > 0) {
    /* TODO: this is a worst-case allocation */
    path = malloc(length + strlen(reldir) + 1);
    if (path) {
      char *pos;
      wai_getExecutablePath(path, (int)length, &dirname_length);
      path[length] = 0;
      pos = strrchr(path, PATH_SEP);
      if (pos) {
        strcpy(pos + 1, reldir);
        if (0 != fileexists(path)) {
          free(path);
          path = NULL;
        }
      } else {
        free(path);
        path = NULL;
      }
    }
  }
  if (path) {
    bindtextdomain("echeck", path);
    free(path);
  } else if (fileexists(reldir) == 0) {
    bindtextdomain("echeck", reldir);
  }
  else {
    bindtextdomain("echeck", "/usr/share/locale");
  }
  textdomain("echeck");
}
#endif

int echeck_main(int argc, char *argv[]) {
  int faction_count = 0, unit_count = 0, nextarg = 1, i;
#ifdef HAVE_GETTEXT
  init_intl();
#endif
#if macintosh
  argc = ccommand(&argv); /* consolenabruf der parameter fuer  macintosh
                             added 15.6.00 chartus */
#endif

  ERR = stderr;
  echeck_init();
  ERR = stdout;

  if (filename) {
    parse_options(filename, 1);
  }

  if (argc > 1) {
    nextarg = check_options(argc, argv, 1, 1);
  }
  if (argc <= 1)
    printhelp(argc, argv, 0);
  filesread = readfiles();

  if (!filesread) {
    files_not_found(ERR);
    help_keys('f'); /* help_keys() macht exit() */
  }

  if (filesread != HAS_ALL) {
    sprintf(checked_buf, "%s: ", _("Missing files containing"));
    if (!(filesread & HAS_PARAM)) {
      Scat(Errors[MISSFILEPARAM]);
    }
    if (!(filesread & HAS_ITEM)) {
      Scat(Errors[MISSFILEITEM]);
    }
    if (!(filesread & HAS_SKILL)) {
      Scat(Errors[MISSFILESKILL]);
    }
    if (!(filesread & HAS_KEYWORD)) {
      Scat(Errors[MISSFILECMD]);
    }
    if (!(filesread & HAS_DIRECTION)) {
      Scat(Errors[MISSFILEDIR]);
    }
    fputs(checked_buf, ERR);
    return 5;
  }

  F = stdin;

  for (i = nextarg; i < argc; i++) {
    int bom;
    F = fopen(argv[i], "rt");
    if (!F) {
      fprintf(ERR, _("Cannot read file %s."), argv[i]);
      fputc('\n', ERR);
      return 2;
    } else {
      filename = argv[i];
      if (!compile) {
        fprintf(ERR, _("Processing file '%s'."), argv[i]);
        fputc('\n', ERR);
      }
      bom = fgetc(F);
      if (bom == 0xef) {
        fseek(F, 3, SEEK_SET);
      } else {
        fseek(F, 0, SEEK_SET);
      }
      process_order_file(&faction_count, &unit_count);
    }
  }
  /*
   * Falls es keine input Dateien gab, ist F immer noch auf stdin
   * gesetzt
   */
  if (F == stdin)
    process_order_file(&faction_count, &unit_count);

  free(g_path);

  if (compile) {
    return error_count;
  }

  char factions[32];
  char units[32];
  SNPRINTF(factions, sizeof(factions),
           ngettext("%d faction", "%d factions", faction_count), faction_count);
  SNPRINTF(units, sizeof(units), ngettext("%d unit", "%d units", unit_count),
           unit_count);
  fprintf(ERR, _("Orders have been read for %s and %s."), factions, units);
  fputc('\n', ERR);

  if (unit_count == 0) {
    fputs(_("Please make sure you have sent in your orders properly.\nRemember "
            "that orders must not be sent as HTML or word-documents."),
          ERR);
    fputc('\n', ERR);
    return -42;
  }

  if (!error_count && !warning_count && faction_count && unit_count) {
    fputs(_("The orders look good."), ERR);
    fputc('\n', ERR);
  }

  if (warning_count > 0) {
    fprintf(
      ERR,
      ngettext("Detected %d warning", "Detected %d warnings", warning_count),
      warning_count);
    if (error_count > 0) {
      fputs(", ", ERR);
      fprintf(ERR, ngettext("%d error", "%d errors", error_count), error_count);
    }
    fputc('.', ERR);
  } else {
    fprintf(ERR,
            ngettext("Detected %d error.", "Detected %d errors.", error_count),
            error_count);
  }
  fputc('\n', ERR);
  return error_count;
}
