#include "command_parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <Arduino.h>

namespace
{
  constexpr size_t kCommandLineLength = 80;

  char lineBuffer[kCommandLineLength];
  size_t lineLength = 0;

  void resetCommand(ParsedCommand &out)
  {
    out = ParsedCommand{};
  }

  char *trim(char *text) // 整理字符串
  {
    while (*text != '\0' && isspace(static_cast<unsigned char>(*text)))
    {
      ++text;
    }

    if (*text == '\0')
    {
      return text;
    }

    char *end = text + strlen(text) - 1;
    while (end > text && isspace(static_cast<unsigned char>(*end)))
    {
      *end = '\0';
      --end;
    }

    return text;
  }

  bool equalsIgnoreCase(const char *left, const char *right)
  {
    while (*left != '\0' && *right != '\0')
    {
      const char a = static_cast<char>(tolower(static_cast<unsigned char>(*left)));
      const char b = static_cast<char>(tolower(static_cast<unsigned char>(*right)));
      if (a != b)
      {
        return false;
      }
      ++left;
      ++right;
    }

    return *left == '\0' && *right == '\0';
  }

  bool parseBool(const char *text, bool &value)
  {
    if (equalsIgnoreCase(text, "true"))
    {
      value = true;
      return true;
    }
    if (equalsIgnoreCase(text, "false"))
    {
      value = false;
      return true;
    }
    return false;
  }

  bool parseInt(const char *text, int &value)
  {
    if (text == nullptr || *text == '\0')
    {
      return false;
    }

    char *end = nullptr;
    const long parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0') // 检验是否失败
    {
      return false;
    }

    value = static_cast<int>(parsed);
    return true;
  }

  bool parseLine(char *line, ParsedCommand &out)
  {
    resetCommand(out);

    char *trimmed = trim(line);
    char *name = strtok(trimmed, " \t");
    char *arg = strtok(nullptr, " \t");
    char *extra = strtok(nullptr, " \t");

    if (name == nullptr || arg == nullptr || extra != nullptr)
    {
      return false;
    }

    int intValue = 0;
    bool boolValue = false;

    if (equalsIgnoreCase(name, "setemo"))
    {
      if (!parseInt(arg, intValue))
      {
        return false;
      }
      out.type = CommandType::SetEmotion;
      out.intValue = intValue;
      out.valid = true;
      return true;
    }

    if (equalsIgnoreCase(name, "setful"))
    {
      if (!parseBool(arg, boolValue))
      {
        return false;
      }
      out.type = CommandType::SetFull;
      out.boolValue = boolValue;
      out.valid = true;
      return true;
    }

    if (equalsIgnoreCase(name, "setmot"))
    {
      if (!parseInt(arg, intValue) || intValue < 0 || intValue > 4)
      {
        return false;
      }
      out.type = CommandType::SetMotion;
      out.intValue = intValue;
      out.motionValue = static_cast<MotionMode>(intValue);
      out.valid = true;
      return true;
    }

    if (equalsIgnoreCase(name, "setslp") || equalsIgnoreCase(name, "setsleep"))
    {
      if (!parseBool(arg, boolValue))
      {
        return false;
      }
      out.type = CommandType::SetSleep;
      out.boolValue = boolValue;
      out.valid = true;
      return true;
    }

    return false;
  }

} // namespace

bool commandParseText(const char *text, ParsedCommand &out)
{
  resetCommand(out);

  if (text == nullptr)
  {
    return false;
  }

  char buffer[kCommandLineLength];
  strncpy(buffer, text, kCommandLineLength - 1);
  buffer[kCommandLineLength - 1] = '\0';

  return parseLine(buffer, out);
}

bool commandPoll(ParsedCommand &out)
{
  resetCommand(out);

  while (Serial.available() > 0)
  {
    const char c = static_cast<char>(Serial.read());

    if (c == '\r' || c == '\n')
    {
      if (lineLength == 0)
      {
        continue;
      }

      lineBuffer[lineLength] = '\0';
      lineLength = 0;
      commandParseText(lineBuffer, out);
      return true;
    }

    if (lineLength + 1 >= kCommandLineLength)
    {
      lineLength = 0;
      return true;
    }

    lineBuffer[lineLength++] = c;
  }

  return false;
}
