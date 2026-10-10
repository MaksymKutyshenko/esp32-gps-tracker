#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Splits one console line into a command and arguments, in place. The
// console grammar is deliberately tiny:
//
//   help | show | diag | defaults | save | mount
//   set <key> <value>
//
// Pure logic, no ESP-IDF dependency; unit-tested on the host.
class CommandParser
{
public:
    enum class Command : uint8_t
    {
        None,      // empty line
        Unknown,
        Help,
        Show,
        Diag,
        Defaults,
        Save,
        Mount,     // retry the storage card now
        Set,
    };

    struct Parsed
    {
        Command command;
        const char *key;    // Set only
        const char *value;  // Set only; may contain spaces? no — single token
    };

    // Modifies `line` (inserts NULs). Pointers in the result point into it.
    static Parsed parse(char *line)
    {
        Parsed result = {Command::None, nullptr, nullptr};
        char *tokens[3] = {};
        size_t count = tokenize(line, tokens, 3);
        if (count == 0)
        {
            return result;
        }

        const char *word = tokens[0];
        if (strcmp(word, "help") == 0) result.command = Command::Help;
        else if (strcmp(word, "show") == 0) result.command = Command::Show;
        else if (strcmp(word, "diag") == 0) result.command = Command::Diag;
        else if (strcmp(word, "defaults") == 0) result.command = Command::Defaults;
        else if (strcmp(word, "save") == 0) result.command = Command::Save;
        else if (strcmp(word, "mount") == 0) result.command = Command::Mount;
        else if (strcmp(word, "set") == 0)
        {
            if (count != 3)
            {
                result.command = Command::Unknown;
                return result;
            }
            result.command = Command::Set;
            result.key = tokens[1];
            result.value = tokens[2];
        }
        else result.command = Command::Unknown;
        return result;
    }

    // Parses an unsigned decimal; false on anything else.
    static bool parseUnsigned(const char *text, uint32_t &out)
    {
        if (text == nullptr || *text == '\0')
        {
            return false;
        }
        uint64_t value = 0;
        for (; *text != '\0'; ++text)
        {
            if (*text < '0' || *text > '9')
            {
                return false;
            }
            value = value * 10 + static_cast<uint64_t>(*text - '0');
            if (value > 0xFFFFFFFFull)
            {
                return false;
            }
        }
        out = static_cast<uint32_t>(value);
        return true;
    }

private:
    static size_t tokenize(char *line, char **tokens, size_t capacity)
    {
        size_t count = 0;
        char *p = line;
        while (*p != '\0' && count < capacity)
        {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            {
                ++p;
            }
            if (*p == '\0')
            {
                break;
            }
            tokens[count++] = p;
            while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
            {
                ++p;
            }
            if (*p != '\0')
            {
                *p++ = '\0';
            }
        }
        return count;
    }
};
