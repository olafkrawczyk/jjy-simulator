#pragma once

#include <time.h>

void txBegin();
void txStartWindow(time_t end);
void txStopWindow();
bool txWindowActive();
time_t txWindowEnd();
