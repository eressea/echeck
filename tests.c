#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "echeck.h"
#include "CuTest.h"

#define BUFSIZE 65000

static int unit_no = 0;

static char input_buf[BUFSIZE];
static char msg_buf[BUFSIZE];

static void test_orders(CuTest *tc, const char *input, int exp_warnings,
                        int exp_errors) {
  char *order, *orders;
  size_t sz = 1 + strlen(input);

  orders = malloc(sz);
  if (orders) {
    memcpy(orders, input, sz);
  }
  set_order_unit(newunit(++unit_no, 0));
  line_no = 0;
  error_count = warning_count = 0;

  order = strtok(orders, "\n");
  while (order) {
    input_buf[0] = 0;
    strcat(input_buf, order);
    strcat(input_buf, "\n");
    mock_input(input_buf);
    checkanorder(getbuf());
    order = strtok(NULL, "\n");
  }

  sprintf(msg_buf,
          exp_errors ? "errors expected in '%s'" : "no errors expected in '%s'",
          orders);
  CuAssertIntEquals_Msg(tc, msg_buf, exp_errors, error_count);
  sprintf(msg_buf,
          exp_warnings ? "warnings expected in '%s'"
                       : "no warnings expected in '%s'",
          orders);
  CuAssertIntEquals_Msg(tc, msg_buf, exp_warnings, warning_count);
}

static void assert_long_order(CuTest *tc, int expected_line) {
  CuAssertIntEquals_Msg(tc, "long order line", expected_line,
                        get_long_order_line());
}

static void test_process_nothing(CuTest *tc) {
  int faction_count = 0, unit_count = 0;

  error_count = warning_count = 0;
  mock_input("\n");
  process_order_file(&faction_count, &unit_count);
  CuAssertIntEquals(tc, 0, faction_count);
  CuAssertIntEquals(tc, 0, unit_count);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_process_faction(CuTest *tc) {
  int faction_count = 0, unit_count = 0;

  error_count = warning_count = 0;
  mock_input("ERESSEA 1 \"password\"\n");
  process_order_file(&faction_count, &unit_count);
  CuAssertIntEquals(tc, 1, faction_count);
  CuAssertIntEquals(tc, 0, unit_count);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_process_unit(CuTest *tc) {
  int faction_count = 0, unit_count = 0;

  error_count = warning_count = 0;
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\nNAECHSTER\n");
  process_order_file(&faction_count, &unit_count);
  CuAssertIntEquals(tc, 1, faction_count);
  CuAssertIntEquals(tc, 1, unit_count);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_nothing(CuTest *tc) { CuAssertTrue(tc, 1); }

static void test_getbuf(CuTest *tc) {
  mock_input("herp derp\nherpderp\n  herp   derp\nherp \"derp  derp\"\n");
  CuAssertStrEquals(tc, "herp derp", getbuf());
  CuAssertStrEquals(tc, "herpderp", getbuf());
  CuAssertStrEquals(tc, "herp derp", getbuf());
  CuAssertStrEquals(tc, "herp derp~derp", getbuf());
}

static void test_igetstr(CuTest *tc) {
  CuAssertStrEquals(tc, "herp", igetstr("herp derp"));
  CuAssertStrEquals(tc, "derp", igetstr(0));
}

static void test_give_each(CuTest *tc) {
  set_order_unit(newunit(1, 0));
  mock_input("GIB 2 JE 1 SILBER\n");
  error_count = warning_count = 0;
  checkanorder(getbuf());
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_make_temp(CuTest *tc) {
  set_order_unit(newunit(1, 0));
  mock_input(
    "ERESSEA 1 \"password\"\nEINHEIT 1\nMACHE TEMP 1\nENDE\nNAECHSTER\n");
  error_count = warning_count = 0;
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_reserve_each(CuTest *tc) {
  set_order_unit(newunit(1, 0));
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\n@RESERVIERE je 1 Pferd\nNAECHSTER\n");
  error_count = warning_count = 0;
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_destroy(CuTest *tc) {
  set_order_unit(newunit(1, 0));
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\nZERSTOERE\nNAECHSTER\n");
  error_count = warning_count = 0;
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_destroy_level(CuTest *tc) {
  set_order_unit(newunit(1, 0));
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\nZERSTOERE 4\nNAECHSTER\n");
  error_count = warning_count = 0;
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 0, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_destroy_unhappy(CuTest *tc) {
  set_order_unit(newunit(1, 0));
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\nZERSTOERE BURG\nNAECHSTER\n");
  error_count = warning_count = 0;
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 1, error_count);
  CuAssertIntEquals(tc, 0, warning_count);
}

static void test_destroy_street(CuTest *tc) {
  test_orders(tc, "ZERSTOERE 1 STRASSE O", 0, 0);
}

static void test_destroy_street_direction(CuTest *tc) {
  set_order_unit(newunit(1, 0));
  mock_input("ERESSEA 1 \"password\"\nEINHEIT 1\nZERSTOERE 1 STRASSE "
             "L\nEINHEIT 2\nZERSTOERE NO\nNAECHSTER\n");
  error_count = warning_count = 0;
  process_order_file(0, 0);
  CuAssertIntEquals(tc, 2, error_count);
  CuAssertIntEquals(tc, 1, warning_count);
}

static void test_entertain(CuTest *tc) {
  test_orders(tc, "UNTERHALTE", 0, 0);
  assert_long_order(tc, 1);
}

static void test_claim_nothing(CuTest *tc) {
  test_orders(tc, "BEANSPRUCHE 1", 0, 1);
  test_orders(tc, "BEANSPRUCHE Holz", 0, 1);
  test_orders(tc, "BEANSPRUCHE 1 bla", 1, 0);
}

static void test_check_additional_parameters(CuTest *tc) {
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
}

static void test_plant(CuTest *tc) {
  test_orders(tc, "PFLANZE KRÄUTER", 0, 0);
  assert_long_order(tc, 1);
  test_orders(tc, "PFLANZE 2 KRÄUTER", 0, 0);
  test_orders(tc, "PFLANZE 3 BAEUME", 0, 0);
  test_orders(tc, "PFLANZE 4 Mallornsamen", 0, 0);
  test_orders(tc, "PFLANZE 5 Samen", 0, 0);
  /* test_orders(tc, "PFLANZE 5 Silber", 1, 0); */
  test_orders(tc, "PFLANZE", 0, 1);
  test_orders(tc, "PFLANZE 1 2", 1, 0);
  /* test_orders(tc, "PFLANZE 1 BAEUME Samen", 1, 0); */
}

static void test_breed(CuTest *tc) {
  test_orders(tc, "ZUECHTE PFERDE", 0, 0);
  assert_long_order(tc, 1);
  test_orders(tc, "ZUECHTE KRAEUTER", 0, 0);
  assert_long_order(tc, 1);
  test_orders(tc, "ZUECHTE 3 KRAEUTER", 0, 0);

  test_orders(tc, "ZUECHTE 3 PFERDE", 0, 1);
  test_orders(tc, "ZUECHTE", 0, 1);
}

static void test_origin(CuTest *tc) {
  test_orders(tc, "URSPRUNG 1 -1", 0, 0);
  test_orders(tc, "URSPRUNG 1", 0, 1);
  test_orders(tc, "URSPRUNG", 0, 1);
}

static void test_language(CuTest *tc) {
  test_orders(tc, "SPRACHE de", 0, 0);
  test_orders(tc, "SPRACHE de", 0, 0);
  /* test_orders(tc, "SPRACHE 1", 0, 1); */
  test_orders(tc, "SPRACHE", 0, 1);
  /* test_orders(tc, "SPRACHE a b", 1, 0); */
}

int AddTestSuites(CuSuite *suite, const char *args) {
  CuSuite *cs;
  char names[256], *name;
  size_t len;
  if (!args || strcmp(args, "all") == 0) {
    args = "reserve,echeck,process,give,claim,entertain,plant,common";
  }
  len = strlen(args);
  if (len >= sizeof(names)) {
    len = sizeof(names) - 1;
  }
  memcpy(names, args, len);
  names[len] = 0;
  name = strtok(names, ",");

  while (name) {
    verbose = 0;
    brief = 2;
    show_warnings = 1;

    if (strcmp(name, "echeck") == 0) {
      cs = CuSuiteNew();
      SUITE_ADD_TEST(cs, test_getbuf);
      SUITE_ADD_TEST(cs, test_igetstr);
      SUITE_ADD_TEST(cs, test_nothing);
      CuSuiteAddSuite(suite, cs);
    } else if (strcmp(name, "process") == 0) {
      cs = CuSuiteNew();
      SUITE_ADD_TEST(cs, test_process_nothing);
      SUITE_ADD_TEST(cs, test_process_faction);
      SUITE_ADD_TEST(cs, test_process_unit);
      CuSuiteAddSuite(suite, cs);
    } else if (strcmp(name, "give") == 0) {
      cs = CuSuiteNew();
      SUITE_ADD_TEST(cs, test_give_each);
      SUITE_ADD_TEST(cs, test_make_temp);
      CuSuiteAddSuite(suite, cs);
    } else if (strcmp(name, "destroy") == 0) {
      cs = CuSuiteNew();
      SUITE_ADD_TEST(cs, test_destroy);
      SUITE_ADD_TEST(cs, test_destroy_level);
      SUITE_ADD_TEST(cs, test_destroy_unhappy);
      SUITE_ADD_TEST(cs, test_destroy_street);
      SUITE_ADD_TEST(cs, test_destroy_street_direction);
      CuSuiteAddSuite(suite, cs);
    } else if (strcmp(name, "entertain") == 0) {
      cs = CuSuiteNew();
      SUITE_ADD_TEST(cs, test_entertain);
      CuSuiteAddSuite(suite, cs);
    } else if (strcmp(name, "reserve") == 0) {
      cs = CuSuiteNew();
      SUITE_ADD_TEST(cs, test_reserve_each);
      CuSuiteAddSuite(suite, cs);
    } else if (strcmp(name, "claim") == 0) {
      cs = CuSuiteNew();
      SUITE_ADD_TEST(cs, test_claim_nothing);
      CuSuiteAddSuite(suite, cs);
    } else if (strcmp(name, "plant") == 0) {
      cs = CuSuiteNew();
      SUITE_ADD_TEST(cs, test_plant);
      SUITE_ADD_TEST(cs, test_breed);
      CuSuiteAddSuite(suite, cs);
    } else if (strcmp(name, "common") == 0) {
      cs = CuSuiteNew();
      if (0)
        SUITE_ADD_TEST(cs, test_check_additional_parameters);
      SUITE_ADD_TEST(cs, test_origin);
      SUITE_ADD_TEST(cs, test_language);
      CuSuiteAddSuite(suite, cs);
    }
    name = strtok(0, ",");
  }
  return 0;
}

const char *run_tests = 0;

int usage(void) {
  return 0;
}

int main(int argc, char *argv[])
{
  CuSuite *suite = CuSuiteNew();
  CuString *output = CuStringNew();

  echeck_init();
  readfiles();
  AddTestSuites(suite, "all");
  CuSuiteRun(suite);
  CuSuiteSummary(suite, output);
  CuSuiteDetails(suite, output);
  printf("%s\n", output->buffer);

  return suite->failCount;
}
