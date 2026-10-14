#pragma once

// Compares "1.2.3" style versions (optional leading 'v', suffix after a non-digit ignored).
// Unparsable versions such as "dev" are older than every release. Returns -1, 0 or 1.
int versionCompare(const char *a, const char *b);
