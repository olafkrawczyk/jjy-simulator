#include "timezones.h"

#include <string.h>

#define CET_RULE "CET-1CEST,M3.5.0,M10.5.0/3"
#define EET_RULE "EET-2EEST,M3.5.0/3,M10.5.0/4"

const TzEntry kTimezones[] = {
    {"honolulu", "Honolulu", "Pacific/Honolulu", "HST10"},
    {"anchorage", "Anchorage", "America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"los_angeles", "Los Angeles / Vancouver", "America/Los_Angeles America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
    {"denver", "Denver / Edmonton", "America/Denver America/Edmonton", "MST7MDT,M3.2.0,M11.1.0"},
    {"phoenix", "Phoenix", "America/Phoenix", "MST7"},
    {"chicago", "Chicago / Winnipeg", "America/Chicago America/Winnipeg", "CST6CDT,M3.2.0,M11.1.0"},
    {"mexico_city", "Mexico City", "America/Mexico_City", "CST6"},
    {"new_york", "New York / Toronto", "America/New_York America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},
    {"halifax", "Halifax", "America/Halifax", "AST4ADT,M3.2.0,M11.1.0"},
    {"sao_paulo", "S\xC3\xA3o Paulo", "America/Sao_Paulo", "<-03>3"},
    {"buenos_aires", "Buenos Aires", "America/Argentina/Buenos_Aires America/Buenos_Aires", "<-03>3"},
    {"reykjavik", "Reykjavik", "Atlantic/Reykjavik", "GMT0"},
    {"london", "London", "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"dublin", "Dublin", "Europe/Dublin", "GMT0IST,M3.5.0/1,M10.5.0"},
    {"lisbon", "Lisbon", "Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"madrid", "Madrid", "Europe/Madrid", CET_RULE},
    {"paris", "Paris", "Europe/Paris", CET_RULE},
    {"amsterdam", "Amsterdam / Brussels", "Europe/Amsterdam Europe/Brussels", CET_RULE},
    {"berlin", "Berlin", "Europe/Berlin", CET_RULE},
    {"rome", "Rome", "Europe/Rome", CET_RULE},
    {"stockholm", "Stockholm / Oslo / Copenhagen", "Europe/Stockholm Europe/Oslo Europe/Copenhagen", CET_RULE},
    {"prague", "Prague / Vienna", "Europe/Prague Europe/Vienna", CET_RULE},
    {"warsaw", "Warsaw", "Europe/Warsaw", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
    {"lagos", "Lagos", "Africa/Lagos", "WAT-1"},
    {"athens", "Athens", "Europe/Athens", EET_RULE},
    {"bucharest", "Bucharest", "Europe/Bucharest", EET_RULE},
    {"helsinki", "Helsinki", "Europe/Helsinki", EET_RULE},
    {"kyiv", "Kyiv", "Europe/Kyiv Europe/Kiev", EET_RULE},
    {"cairo", "Cairo", "Africa/Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"johannesburg", "Johannesburg", "Africa/Johannesburg", "SAST-2"},
    {"istanbul", "Istanbul", "Europe/Istanbul", "<+03>-3"},
    {"moscow", "Moscow", "Europe/Moscow", "MSK-3"},
    {"nairobi", "Nairobi", "Africa/Nairobi", "EAT-3"},
    {"dubai", "Dubai", "Asia/Dubai", "<+04>-4"},
    {"karachi", "Karachi", "Asia/Karachi", "PKT-5"},
    {"delhi", "Delhi / Mumbai", "Asia/Kolkata Asia/Calcutta", "IST-5:30"},
    {"dhaka", "Dhaka", "Asia/Dhaka", "<+06>-6"},
    {"bangkok", "Bangkok / Hanoi", "Asia/Bangkok Asia/Ho_Chi_Minh Asia/Saigon", "<+07>-7"},
    {"jakarta", "Jakarta", "Asia/Jakarta", "WIB-7"},
    {"singapore", "Singapore / Kuala Lumpur", "Asia/Singapore Asia/Kuala_Lumpur", "<+08>-8"},
    {"hong_kong", "Hong Kong", "Asia/Hong_Kong", "HKT-8"},
    {"shanghai", "Beijing / Shanghai", "Asia/Shanghai", "CST-8"},
    {"taipei", "Taipei", "Asia/Taipei", "CST-8"},
    {"manila", "Manila", "Asia/Manila", "PST-8"},
    {"perth", "Perth", "Australia/Perth", "AWST-8"},
    {"seoul", "Seoul", "Asia/Seoul", "KST-9"},
    {"tokyo", "Tokyo", "Asia/Tokyo", "JST-9"},
    {"adelaide", "Adelaide", "Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"brisbane", "Brisbane", "Australia/Brisbane", "AEST-10"},
    {"sydney", "Sydney / Melbourne", "Australia/Sydney Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"auckland", "Auckland", "Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"utc", "UTC (no daylight saving)", "UTC Etc/UTC", "UTC0"},
};

const size_t kTimezoneCount = sizeof(kTimezones) / sizeof(kTimezones[0]);

const TzEntry *tzFindById(const char *id) {
  for (size_t i = 0; i < kTimezoneCount; ++i) {
    if (strcmp(kTimezones[i].id, id) == 0) return &kTimezones[i];
  }
  return nullptr;
}
