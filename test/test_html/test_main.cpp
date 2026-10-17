#include <unity.h>

#include <string>

#include "html.h"

void setUp() {}
void tearDown() {}

static bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

static Schedule twoSlots() {
  Schedule s = Schedule();
  s.slotMinutes[0] = 76;
  s.slotMinutes[1] = 976;
  s.count = 2;
  s.windowMinutes = 5;
  return s;
}

void test_html_escape() {
  TEST_ASSERT_EQUAL_STRING("&lt;a href=&quot;x&quot;&gt;&#39;&amp;&#39;&lt;/a&gt;",
                           htmlEscape("<a href=\"x\">'&'</a>").c_str());
}

void test_json_escape() {
  TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c\\n\\u003c/script\\u003e", jsonEscape("a\"b\\c\n</script>").c_str());
}

void test_setup_page() {
  SetupPageView v;
  v.networks.push_back("Home");
  v.networks.push_back("<evil>");
  v.selectedSsid = "Home";
  v.tzId = "warsaw";
  v.schedule = twoSlots();
  v.autoDetectTz = true;
  v.error = "Bad & wrong";
  const std::string page = renderSetupPage(v);
  TEST_ASSERT_TRUE(contains(page, "<option value=\"Home\" selected>Home</option>"));
  TEST_ASSERT_TRUE(contains(page, "&lt;evil&gt;"));
  TEST_ASSERT_FALSE(contains(page, "<evil>"));
  TEST_ASSERT_TRUE(contains(page, "Bad &amp; wrong"));
  TEST_ASSERT_TRUE(contains(page, "name=\"slot\" required value=\"01:16\""));
  TEST_ASSERT_TRUE(contains(page, "name=\"slot\" required value=\"16:16\""));
  TEST_ASSERT_TRUE(contains(page, "value=\"warsaw\" data-iana=\"Europe/Warsaw\" selected"));
  TEST_ASSERT_TRUE(contains(page, "resolvedOptions().timeZone"));
  TEST_ASSERT_TRUE(contains(page, "action=\"/setup\""));
}

void test_setup_page_other_network_when_unmatched() {
  SetupPageView v;
  v.selectedSsid = "Hidden";
  v.tzId = "warsaw";
  v.schedule = twoSlots();
  const std::string page = renderSetupPage(v);
  TEST_ASSERT_TRUE(contains(page, "<option value=\"\" selected>"));
  TEST_ASSERT_TRUE(contains(page, "name=\"ssid_other\" autocapitalize=\"none\" autocorrect=\"off\" value=\"Hidden\""));
}

void test_setup_page_preselects_first_network() {
  SetupPageView v;
  v.networks.push_back("Strongest");
  v.networks.push_back("Weaker");
  v.tzId = "warsaw";
  v.schedule = twoSlots();
  const std::string page = renderSetupPage(v);
  TEST_ASSERT_TRUE(contains(page, "<option value=\"Strongest\" selected>Strongest</option>"));
  TEST_ASSERT_TRUE(contains(page, "<option value=\"\">Other network&hellip;</option>"));
}

void test_connecting_page_uses_hostname() {
  const std::string page = renderConnectingPage("jjy-a1b2");
  TEST_ASSERT_TRUE(contains(page, "<a href=\"http://jjy-a1b2.local\">http://jjy-a1b2.local</a>"));
  TEST_ASSERT_FALSE(contains(page, "http://jjy.local"));
}

void test_settings_page() {
  SettingsPageView v;
  v.localTime = "13:18";
  v.nextBroadcast = "16:16";
  v.tzId = "tokyo";
  v.schedule = twoSlots();
  v.hostname = "jjy-a1b2";
  v.version = "1.2.0";
  const std::string page = renderSettingsPage(v);
  TEST_ASSERT_TRUE(contains(page, "value=\"tokyo\" data-iana=\"Asia/Tokyo\" selected"));
  TEST_ASSERT_TRUE(contains(page, "Address http://jjy-a1b2.local"));
  TEST_ASSERT_TRUE(contains(page, "Next broadcast at 16:16"));
  TEST_ASSERT_FALSE(contains(page, "name=\"freq\""));
  TEST_ASSERT_TRUE(contains(page, "Software version 1.2.0"));
  TEST_ASSERT_TRUE(contains(page, "action=\"/update/check\""));
  TEST_ASSERT_FALSE(contains(page, "action=\"/update/install\""));
  TEST_ASSERT_TRUE(contains(page, "action=\"/forget\""));
  TEST_ASSERT_FALSE(contains(page, "resolvedOptions().timeZone"));
}

void test_settings_page_update_and_broadcasting() {
  SettingsPageView v;
  v.localTime = "13:18";
  v.broadcasting = true;
  v.tzId = "warsaw";
  v.schedule = twoSlots();
  v.version = "1.2.0";
  v.updateVersion = "1.3.0";
  const std::string page = renderSettingsPage(v);
  TEST_ASSERT_TRUE(contains(page, "Broadcasting now"));
  TEST_ASSERT_TRUE(contains(page, "Install version 1.3.0"));
  TEST_ASSERT_TRUE(contains(page, "action=\"/update/install\""));
}

void test_waiting_for_time() {
  SettingsPageView v;
  v.tzId = "warsaw";
  v.schedule = twoSlots();
  TEST_ASSERT_TRUE(contains(renderSettingsPage(v), "Waiting for the time from the internet"));
}

void test_no_external_resources() {
  SetupPageView setup;
  setup.tzId = "warsaw";
  setup.schedule = twoSlots();
  SettingsPageView settings;
  settings.tzId = "warsaw";
  settings.schedule = twoSlots();
  const std::string pages[] = {renderSetupPage(setup), renderSettingsPage(settings), renderConnectingPage("jjy-a1b2"),
                               renderUpdatingPage("1.0.0"), renderMessagePage("t", "x")};
  for (const std::string &p : pages) {
    TEST_ASSERT_FALSE(contains(p, "<script src"));
    TEST_ASSERT_FALSE(contains(p, "<link"));
    TEST_ASSERT_TRUE(contains(p, "<style>"));
  }
}

void test_updating_page_embeds_version_safely() {
  TEST_ASSERT_TRUE(contains(renderUpdatingPage("1.0.0"), "var cur=\"1.0.0\""));
  TEST_ASSERT_TRUE(contains(renderUpdatingPage("</script>"), "\\u003c/script\\u003e"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_html_escape);
  RUN_TEST(test_json_escape);
  RUN_TEST(test_setup_page);
  RUN_TEST(test_setup_page_other_network_when_unmatched);
  RUN_TEST(test_setup_page_preselects_first_network);
  RUN_TEST(test_connecting_page_uses_hostname);
  RUN_TEST(test_settings_page);
  RUN_TEST(test_settings_page_update_and_broadcasting);
  RUN_TEST(test_waiting_for_time);
  RUN_TEST(test_no_external_resources);
  RUN_TEST(test_updating_page_embeds_version_safely);
  return UNITY_END();
}
