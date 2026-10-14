#include "version_compare.h"

#include <ctype.h>

namespace {

bool parseVersion(const char *text, int parts[3]) {
  parts[0] = parts[1] = parts[2] = 0;
  if (text == nullptr) return false;
  if (*text == 'v' || *text == 'V') ++text;
  if (!isdigit(static_cast<unsigned char>(*text))) return false;
  for (int i = 0; i < 3; ++i) {
    if (!isdigit(static_cast<unsigned char>(*text))) break;
    int value = 0;
    while (isdigit(static_cast<unsigned char>(*text))) {
      value = value * 10 + (*text - '0');
      ++text;
    }
    parts[i] = value;
    if (*text != '.') break;
    ++text;
  }
  return true;
}

}  // namespace

int versionCompare(const char *a, const char *b) {
  int pa[3];
  int pb[3];
  const bool validA = parseVersion(a, pa);
  const bool validB = parseVersion(b, pb);
  if (!validA || !validB) return (validA ? 1 : 0) - (validB ? 1 : 0);
  for (int i = 0; i < 3; ++i) {
    if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
  }
  return 0;
}
