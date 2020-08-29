#include <CuTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

struct unit;
extern int error_count;
extern int warning_count;
extern int verbose;
extern int brief;
extern char show_warnings;
extern char *echeck_rules;
extern int line_no;
extern char no_comment;
extern int get_long_order_line();
extern void mock_input(const char * input);
extern void set_output(FILE *out, FILE *err);
extern char *getbuf(void);
extern char *igetstr(char *);
extern int btoi(char *s);
extern char *itob(int i);
extern void checkgiving(void);
extern void checkanorder(char *);
extern struct unit *newunit(int n, int t);
extern void set_order_unit(struct unit * u);
void process_order_file(int *faction_count, int *unit_count);

#define BUFSIZE 65000

#define DEFAULT_VERB 0
#define DEFAULT_BRIEF 2
#define DEFAULT_WARN 1

#define ERROR_FILE ".#echeck.test.error#"
#define OUTPUT_FILE ".#echeck.test.echo#"

static FILE *OUT = NULL, *ERR = NULL;

static int unit_no = 0;

static char input_buf[BUFSIZE];
static char msg_buf[BUFSIZE];

static void test_orders(CuTest * tc, char * orders, int exp_warnings, int exp_errors) {
  char * order;
  char * buf;

  set_order_unit(newunit(++unit_no, 0));
  line_no = 0;
  error_count = warning_count = 0;

  buf = strdup(orders);
  order = strtok(buf, "\n");
  while (order) {
    input_buf[0] = 0;
    strcat(input_buf, order);
    strcat(input_buf, "\n");
    mock_input(input_buf);
    checkanorder(getbuf());
    order = strtok(0, "\n");
  }
  free(buf);

  sprintf(msg_buf, exp_errors?"errors expected in '%s'":"no errors expected in '%s'", orders);
  CuAssertIntEquals_Msg(tc, msg_buf, exp_errors, error_count);
  sprintf(msg_buf, exp_warnings?"warnings expected in '%s'":"no warnings expected in '%s'", orders);
  CuAssertIntEquals_Msg(tc, msg_buf, exp_warnings, warning_count);
}

static void assert_long_order(CuTest * tc, int expected_line) {
  CuAssertIntEquals_Msg(tc, "long order line", expected_line, get_long_order_line());
}

void start_output() {
  if (ERR && ERR != stdout && ERR != stderr)
    fclose(ERR);
  ERR = fopen(ERROR_FILE, "w");
  if (OUT && OUT != stdout && OUT != stderr)
    fclose(OUT);
  OUT = fopen(OUTPUT_FILE, "w");
  set_output(OUT, ERR);
}

void reset_output() {
  if (ERR && ERR != stdout && ERR != stderr) {
    fclose(ERR);
    ERR = 0;
  }
  if (OUT && OUT != stdout && OUT != stderr) {
    fclose(OUT);
    OUT = 0;
  }
  remove(OUTPUT_FILE);
  remove(ERROR_FILE);
  set_output(stdout, stdout);
}

char * get_file_content(char *filename) {
  FILE *IN;
  long lSize;
  char *buffer;

  IN = fopen(filename, "rb");
  if (IN) {
    fseek(IN, 0L, SEEK_END);
    lSize = ftell(IN);
    rewind(IN);
    buffer = malloc(sizeof(char) * (lSize + 1));
    if (buffer) {
      if (1 == fread(buffer, lSize, 1, IN)) {
        buffer[lSize] = 0;
        fclose(IN);
        return buffer;
      }
    }
    fclose(IN);
  }
  return NULL;
}

char * get_output() {
  if (ERR)
    fflush(ERR);
  return get_file_content(ERROR_FILE);
}

char * get_echo() {
  if (OUT)
    fflush(OUT);
  return get_file_content(OUTPUT_FILE);
}

void setup_test() {
  no_comment = -128;
  verbose = DEFAULT_VERB;
  brief = DEFAULT_BRIEF;
  show_warnings = DEFAULT_WARN;
  reset_output();
}

static void test_tear_down(CuTest *tc)
{
  reset_output();
}

static void test_process_nothing(CuTest * tc)
{
  int faction_count = 0, unit_count = 0;

  error_count = warning_count = 0;
  mock_input("\n");
  process_order_file(&faction_count, &unit_count);
  CuAssertIntEquals(tc, 0, faction_count);
  CuAssertIntEquals(tc, 0, unit_count);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_process_faction(CuTest * tc)
{
  int faction_count = 0, unit_count = 0;

  error_count = warning_count = 0;
  mock_input("ERESSEA 1 \"password\"\n");
  process_order_file(&faction_count, &unit_count);
  CuAssertIntEquals(tc, 1, faction_count);
  CuAssertIntEquals(tc, 0, unit_count);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_process_feressea(CuTest * tc)
{
  error_count = warning_count = 0;
  mock_input("FERESSEA 1 \"password\"\n");
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 1, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_wrong_next(CuTest * tc)
{
  error_count = warning_count = 0;
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\nNAECHSTER\nEINHEIT 2\nNAECHSTER\n");
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 1, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_orderstart2(CuTest * tc)
{
  int faction_count = 0;
  error_count = warning_count = 0;
  mock_input("PARTEI 1 \"password\"\nNAECHSTER\n");
  process_order_file(&faction_count, 0);
  CuAssertIntEquals(tc, 1, faction_count);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_two_factions(CuTest * tc)
{
  int faction_count = 0, unit_count = 0;

  error_count = warning_count = 0;
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\nNAECHSTER\nERESSEA x \"pw\"\nEINHEIT 2\nNAECHSTER\n");
  process_order_file(&faction_count, &unit_count);
  CuAssertIntEquals(tc, 2, faction_count);
  CuAssertIntEquals(tc, 2, unit_count);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_process_unit(CuTest * tc)
{
  int faction_count = 0, unit_count = 0;

  error_count = warning_count = 0;
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\nNAECHSTER\n");
  process_order_file(&faction_count, &unit_count);
  CuAssertIntEquals(tc, 1, faction_count);
  CuAssertIntEquals(tc, 1, unit_count);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_locale(CuTest * tc)
{
  error_count = warning_count = 0;
  mock_input("ERESSEA 1 \"password\"\nLOCALE de\nNAECHSTER\n");
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_locale_mismatch(CuTest * tc)
{
  error_count = warning_count = 0;
  mock_input("ERESSEA 1 \"password\"\nLOCALE en\nNAECHSTER\n");
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 1, warning_count);
}

static void test_nothing(CuTest * tc)
{
  CuAssertTrue(tc, 1);
}

static void test_getbuf(CuTest * tc)
{
  mock_input("herp derp\nherpderp\n");
  CuAssertStrEquals(tc, "herp derp", getbuf());
  CuAssertStrEquals(tc, "herpderp", getbuf());
}

static void test_igetstr(CuTest * tc)
{
  CuAssertStrEquals(tc, "herp", igetstr("herp derp"));
  CuAssertStrEquals(tc, "derp", igetstr(0));
}

static void test_cont(CuTest *tc)
{
  mock_input("a line\\\n continued\n");
  CuAssertStrEquals(tc, "a linecontinued", getbuf());
  mock_input("a line \\\ncontinued\n");
  CuAssertStrEquals(tc, "a line continued", getbuf());
  mock_input("a \"string \\\ncontinued\"\n");
  CuAssertStrEquals(tc, "a string~continued", getbuf());
  mock_input("a \" \\\nstring continued \"\n");
  CuAssertStrEquals(tc, "a string~continued", getbuf());
}

static void test_give_each(CuTest * tc)
{
  set_order_unit(newunit(++unit_no, 0));
  mock_input("GIB 2 JE 1 SILBER\n");
  error_count = warning_count = 0;
  checkanorder(getbuf());
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_make_temp(CuTest * tc)
{
  set_order_unit(newunit(++unit_no, 0));
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\nMACHE TEMP 1\nENDE\nNAECHSTER\n");
  error_count = warning_count = 0;
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_destroy(CuTest * tc)
{
  set_order_unit(newunit(++unit_no, 0));
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\nZERSTOERE\nNAECHSTER\n");
  error_count = warning_count = 0;
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_destroy_level(CuTest * tc)
{
  set_order_unit(newunit(++unit_no, 0));
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\nZERSTOERE 4\nNAECHSTER\n");
  error_count = warning_count = 0;
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_destroy_unhappy(CuTest * tc)
{
  set_order_unit(newunit(++unit_no, 0));
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\nZERSTOERE BURG\nNAECHSTER\n");
  error_count = warning_count = 0;
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 1, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_destroy_street(CuTest * tc)
{
  test_orders(tc, "ZERSTOERE 1 STRASSE O", 0, 0);
}

static void test_destroy_street_direction(CuTest * tc)
{
  set_order_unit(newunit(++unit_no, 0));
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\nZERSTOERE 1 STRASSE L\nEINHEIT 2\nZERSTOERE NO\nNAECHSTER\n");
  error_count = warning_count = 0;
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 2, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_entertain(CuTest * tc)
{
  test_orders(tc, "UNTERHALTE", 0, 0);
  assert_long_order(tc, 1);
  test_orders(tc, "UNTERHALTE 500", 0, 0);
  assert_long_order(tc, 1);
  test_orders(tc, "UNTERHALTE x", 1, 0);
  assert_long_order(tc, 1);
  test_orders(tc, "GIB x y z\nUNTERHALTE x", 1, 1);
  assert_long_order(tc, 2);
  test_orders(tc, "ALLIANZ VERLASSEN", 0, 1);
  assert_long_order(tc, 0);
}

static void test_claim_nothing(CuTest * tc)
{
  test_orders(tc, "BEANSPRUCHE 1", 0, 1);
  test_orders(tc, "BEANSPRUCHE Holz", 0, 1);
  test_orders(tc, "BEANSPRUCHE 1 bla", 1, 0);
}

static void test_e3(CuTest * tc)
{
  test_orders(tc, "UNTERHALTE", 0, 1);
}

static void test_alliance_happy(CuTest * tc)
{
  test_orders(tc, "ALLIANZ AUSSTOSSEN xy", 0, 0);
  test_orders(tc, "ALLIANZ VERLASSEN", 0, 0);
  test_orders(tc, "ALLIANZ KOMMANDO abc", 0, 0);
  test_orders(tc, "ALLIANZ NEU", 0, 0);
  test_orders(tc, "ALLIANZ EINLADEN abc", 0, 0);
  test_orders(tc, "ALLIANZ BEITRETEN xyz", 0, 0);
  test_orders(tc, "BENENNE ALLIANZ 'Hodor'", 0, 0);
}

static void test_alliance_missing(CuTest * tc)
{
  test_orders(tc, "ALLIANZ", 0, 1);
  test_orders(tc, "ALLIANZ AUSSTOSSEN", 0, 1);
  test_orders(tc, "ALLIANZ KOMMANDO", 0, 1);
  test_orders(tc, "ALLIANZ EINLADEN", 0, 1);
  test_orders(tc, "ALLIANZ BEITRETEN", 0, 1);
  test_orders(tc, "BENENNE ALLIANZ", 1, 0);
}

static void test_alliance_toomuch(CuTest * tc)
{
  test_orders(tc, "ALLIANZ AUSSTOSSEN xy ab", 0, 1);
  test_orders(tc, "ALLIANZ VERLASSEN 1", 0, 1);
  test_orders(tc, "ALLIANZ KOMMANDO abc def", 0, 1);
  test_orders(tc, "ALLIANZ NEU x", 0, 1);
  test_orders(tc, "ALLIANZ EINLADEN abc def", 0, 1);
  test_orders(tc, "ALLIANZ BEITRETEN xyz abc", 0, 1);
  test_orders(tc, "ALLIANZ xyz", 0, 1);
}

static void test_check_additional_parameters(CuTest *tc)
{
  test_orders(tc, "// bla blubb", 0, 0);
  test_orders(tc, "//bla blubb", 0, 1);
  test_orders(tc, "PIRATERIE x y z", 0, 0);
  test_orders(tc, "ZAUBERE \"Sonnenschein\" a b 1 2 3", 0, 0);

  test_orders(tc, "BEFÖRDERE 1", 1, 0);
  test_orders(tc, "FAHRE 1 2", 1, 0);
  test_orders(tc, "PRAEFIX a b c", 1, 0);
  test_orders(tc, "RESERVIERE 1 Silber und Gold", 1, 0);

  test_orders(tc, "ATTACKIERE 1 2 3", 0, 1);
  test_orders(tc, "NACH no nw 1", 0, 1);
  test_orders(tc, "ROUTE no nw pause 1", 0, 1);
  test_orders(tc, "TRANSPORTIERE x y", 0, 1);

  if (strcmp(echeck_rules, "e2") == 0) {
    test_orders(tc, "BEKLAUE x y", 1, 0);
    test_orders(tc, "LEHRE a b c d", 0, 0); /* Personen */
  }
}

static void test_check_quotes(CuTest *tc)
{
  test_orders(tc, "MACHE 1 Rostiges~Kettenhemd", 0, 0);
  test_orders(tc, "MACHE 2 \" Rostiges Kettenhemd \"", 0, 0);
  test_orders(tc, "MACHE 3 'Rostiges Kettenhemd'", 0, 0);
  test_orders(tc, "MACHE 4 \"Rostiges Kettenhemd'", 0, 2);
  test_orders(tc, "MACHE 5 'Rostiges Kettenhemd\"", 0, 2);
  test_orders(tc, "MACHE 6 \"Rostiges~Kettenhemd\"", 0, 0);

  test_orders(tc, "DEFAULT \"ARBEITE\"", 0, 0);
  test_orders(tc, "DEFAULT 'ARBEITE'", 0, 0);
  test_orders(tc, "DEFAULT ARBEITE", 0, 0);

  test_orders(tc, "DEFAULT 'MACHE 1 Schwert'", 0, 0);
  test_orders(tc, "DEFAULT \"MACHE 2 Schwert\"", 0, 0);
  // currently not working
  //  test_orders(tc, "DEFAULT \"MACHE 3 'Rostiges Kettenhemd'\"", 0);
  //  test_orders(tc, "DEFAULT \"MACHE 4 Rostiges~Kettenhemd\"", 0);
  //  test_orders(tc, "DEFAULT \"MACHE 5 'Rostiges~Kettenhemd'\"", 0);
  //  test_orders(tc, "DEFAULT 'MACHE 6 \"Rostiges Kettenhemd\"', 0);

  test_orders(tc, "BENENNE EINHEIT \"a \\\"nice\\\" name\"", 0, 0);
  test_orders(tc, "BENENNE EINHEIT 'parser\\'s nightmare'", 0, 0);
  test_orders(tc, "BENENNE EINHEIT \"parser\\'s nightmare\"", 0, 0);
  test_orders(tc, "BENENNE EINHEIT \"parser\\\"s nightmare\"", 0, 0);
}

static void test_plant(CuTest *tc)
{
  test_orders(tc, "PFLANZE KRÄUTER", 0, 0);
  assert_long_order(tc, 1);
  test_orders(tc, "PFLANZE 2 KRÄUTER", 0, 0);
  test_orders(tc, "PFLANZE 3 BAEUME", 0, 0);
  test_orders(tc, "PFLANZE 4 Mallornsamen", 0, 0);
  test_orders(tc, "PFLANZE 5 Samen", 0, 0);
  /* test_orders(tc, "PFLANZE 5 Silber", 1, 0); */
  test_orders(tc, "PFLANZE", 0, 1);
  test_orders(tc, "PFLANZE 1 2", 1, 0);
  test_orders(tc, "PFLANZE 1 BAEUME Samen", 1, 0);
}

static void test_breed(CuTest *tc)
{
  test_orders(tc, "ZUECHTE PFERDE", 0, 0);
  assert_long_order(tc, 1);
  test_orders(tc, "ZUECHTE KRAEUTER", 0, 0);
  assert_long_order(tc, 1);
  test_orders(tc, "ZUECHTE 3 KRAEUTER", 0, 0);

  test_orders(tc, "ZUECHTE 3 PFERDE", 0, 1);
  test_orders(tc, "ZUECHTE", 0, 1);
}

static void test_origin(CuTest *tc)
{
  test_orders(tc, "URSPRUNG 1 -1", 0, 0);
  test_orders(tc, "URSPRUNG 1", 0, 1);
  test_orders(tc, "URSPRUNG", 0, 1);
}

static void test_language(CuTest *tc)
{
  test_orders(tc, "SPRACHE de", 0, 0);
  test_orders(tc, "SPRACHE de", 0, 0);
  /* test_orders(tc, "SPRACHE 1", 0, 1); */
  test_orders(tc, "SPRACHE", 0, 1);
  test_orders(tc, "SPRACHE a b", 1, 0);
}

static void test_itob(CuTest *tc)
{
  char buf[40];
  sprintf(buf, "-%s-%s-%s-%s", itob(btoi("1111")), itob(btoi("2222")), itob(btoi("3333")), itob(btoi("4444")));
  CuAssertStrEquals(tc, buf, "-1111-2222-3333-4444");
  //  sprintf(buf, "-%s-", itob(btoi("1111111")));
  CuAssertIntEquals(tc, 1, btoi("22222"));
}

static void test_leave_ship_on(CuTest *tc)
{
  char *output;

  show_warnings = 5;
  brief = 0;

  start_output();
  mock_input("ERESSEA 1 \"password\"\nREGION 3,3 ; null\nEINHEIT cap ; Captain [1,20$,Sshp1]\nNACH o\nEINHEIT 2 ; On Ship [1,20$,sshp1]\nNACH w\nEINHEIT 3; a [1,20$]\nNAECHSTER\n");
  error_count = warning_count = 0;
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 1, warning_count);

  output = get_output();
  CuAssertTrue(tc, 0 < strstr(output, "Warnung zur Zeile 5: Einheit 2 auf Schiff shp1 hat sich schon bewegt."));
  free(output);
  reset_output();

  setup_test();
}

static void test_leave_ship_off(CuTest *tc)
{
  show_warnings = 1;
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\nNAECHSTER\n");
  mock_input("ERESSEA 1 \"password\"\nREGION 3,3 ; null\nEINHEIT cap ; Captain [1,20$,Sshp1]\nNACH o\nEINHEIT 2 ; On Ship [1,20$,sshp1]\nNACH w\nNAECHSTER\n");
  error_count = warning_count = 0;
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
  show_warnings = DEFAULT_WARN;
}

static void test_use(CuTest *tc)
{
  test_orders(tc, "BENUTZE 1 Wundsalbe", 0, 0);
  test_orders(tc, "BENUTZE Wundsalbe", 0, 0);
  test_orders(tc, "BENUTZE Holz", 0, 0);
  test_orders(tc, "BENUTZE", 0, 1);
  test_orders(tc, "BENUTZE 1", 0, 1);
  test_orders(tc, "BENUTZE Herpderp", 1, 0);
}

int AddTestSuites(CuSuite * suite, const char * args)
{
  char * names = (args && strcmp(args, "all")!=0) ? strdup(args) : strdup("echeck,process,common,give,destroy,entertain,claim,e3,alliance,plant,ship");
  char * name = strtok(names, ",");

  while (name) {
    setup_test();

    if (strcmp(name, "echeck")==0) {
      SUITE_ADD_TEST(suite, test_getbuf);
      SUITE_ADD_TEST(suite, test_igetstr);
      SUITE_ADD_TEST(suite, test_cont);
      SUITE_ADD_TEST(suite, test_nothing);
    }
    else if (strcmp(name, "process")==0) {
      SUITE_ADD_TEST(suite, test_process_nothing);
      SUITE_ADD_TEST(suite, test_process_faction);
      SUITE_ADD_TEST(suite, test_process_feressea);
      SUITE_ADD_TEST(suite, test_orderstart2);
      SUITE_ADD_TEST(suite, test_wrong_next);
      SUITE_ADD_TEST(suite, test_two_factions);
      SUITE_ADD_TEST(suite, test_process_unit);
      SUITE_ADD_TEST(suite, test_locale);
      SUITE_ADD_TEST(suite, test_locale_mismatch);
    }
    else if (strcmp(name, "give")==0) {
      SUITE_ADD_TEST(suite, test_give_each);
      SUITE_ADD_TEST(suite, test_make_temp);
    }
    else if (strcmp(name, "destroy")==0) {
      SUITE_ADD_TEST(suite, test_destroy);
      SUITE_ADD_TEST(suite, test_destroy_level);
      SUITE_ADD_TEST(suite, test_destroy_unhappy);
      SUITE_ADD_TEST(suite, test_destroy_street);
      SUITE_ADD_TEST(suite, test_destroy_street_direction);
    }
    else if (strcmp(name, "claim")==0) {
      SUITE_ADD_TEST(suite, test_claim_nothing);
    }
    else if (strcmp(name, "plant")==0) {
      SUITE_ADD_TEST(suite, test_plant);
      SUITE_ADD_TEST(suite, test_breed);
    }
    else if (strcmp(name, "common")==0) {
      SUITE_ADD_TEST(suite, test_check_additional_parameters);
      SUITE_ADD_TEST(suite, test_origin);
      SUITE_ADD_TEST(suite, test_check_quotes);
      SUITE_ADD_TEST(suite, test_language);
      SUITE_ADD_TEST(suite, test_use);
    }
    else if (strcmp(name, "ship")==0) {
      SUITE_ADD_TEST(suite, test_itob);
      SUITE_ADD_TEST(suite, test_leave_ship_on);
      SUITE_ADD_TEST(suite, test_leave_ship_off);
    }
    /************* e2 only tests **************/
    else if (strcmp(echeck_rules, "e2") == 0) {
      if (strcmp(name, "entertain") == 0) {
        SUITE_ADD_TEST(suite, test_entertain);
      }
    /************* e3 only tests **************/
    } else if (strcmp(echeck_rules, "e3") == 0) {
      if (strcmp(name, "e3") == 0) {
        SUITE_ADD_TEST(suite, test_e3);
      }
      else if (strcmp(name, "alliance") == 0) {
        SUITE_ADD_TEST(suite, test_alliance_happy);
        SUITE_ADD_TEST(suite, test_alliance_missing);
        SUITE_ADD_TEST(suite, test_alliance_toomuch);
      }
    }
    name = strtok(0, ",");
  }
  SUITE_ADD_TEST(suite, test_tear_down);
  free(names);
  return 0;
}
