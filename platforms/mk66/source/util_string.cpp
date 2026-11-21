#include "util_string.h"
#include <cctype>
#include <cstdlib>
#include <cstring>

// Compares the numbers that a and b start with by value, whatever their length, and moves both
// pointers past the digits. The result is negative, zero or positive, as that of strcmp.
static int compareNumbers(const char*& a, const char*& b)
{
    while (*a == '0')
    {
        a++;
    }
    while (*b == '0')
    {
        b++;
    }

    const char* aDigits = a;
    const char* bDigits = b;

    while (std::isdigit(*a))
    {
        a++;
    }
    while (std::isdigit(*b))
    {
        b++;
    }

    if (a - aDigits != b - bDigits)
    {
        return (a - aDigits) < (b - bDigits) ? -1 : 1;
    }

    return std::strncmp(aDigits, bDigits, a - aDigits);
}

bool compareStrings(const char* a, const char* b)
{
    size_t aLength = std::strlen(a);
    size_t bLength = std::strlen(b);

    while ((*a != 0) && (*b != 0))
    {
        bool isDigitA = std::isdigit(*a);
        bool isDigitB = std::isdigit(*b);

        if (isDigitA && isDigitB)
        {
            int order = compareNumbers(a, b);

            if (order != 0)
            {
                return order < 0;
            }
        }
        else if (isDigitA != isDigitB)
        {
            return isDigitA;
        }
        else
        {
            char tempA = std::tolower(*a);
            char tempB = std::tolower(*b);

            if (tempA == tempB)
            {
                a++;
                b++;
            }
            else
            {
                return tempA < tempB;
            }
        }
    }

    // A name that ends where the other one goes on comes first.
    if (*a != 0 || *b != 0)
    {
        return *a == 0;
    }

    // What is left are names that differ in letter case or leading zeros only. Two equal names
    // must give false both ways: std::sort can leave its range with a comparison that does not.
    return aLength < bLength;
}

void polystrncpy(char* dest, const char* src, int destSize)
{
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = 0;
}
