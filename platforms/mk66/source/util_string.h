#ifndef SOURCE_UTIL_STRING_H_
#define SOURCE_UTIL_STRING_H_

// returns true if a is less than b
// if this algorithm encounters a string of digits in the base string, it is treated as one integer
// that is less than the character being compared
bool compareStrings(const char* a, const char* b);

// copies string like standard strncpy and insert 0 at the last character to avoid memory leaks
// destSize is size of destination buffer (not string length as in standard strncpy)
void polystrncpy(char* dest, const char* src, int destSize);

#endif /* SOURCE_UTIL_STRING_H_ */
