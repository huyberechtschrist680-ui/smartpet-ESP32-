#pragma once

#include "app_types.h"

bool commandPoll(ParsedCommand &out);
bool commandParseText(const char *text, ParsedCommand &out);
