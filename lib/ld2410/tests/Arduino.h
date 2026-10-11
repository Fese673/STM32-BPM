// Minimal Arduino stub for host-side unit testing of ld2410.cpp.
// Only exposes the surface that src/ld2410.cpp actually uses:
//   Stream / Print interface, millis(), F() macro, yield(), HEX/DEC constants.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define HEX 16
#define DEC 10
#define F(s) (s)
typedef const char* __FlashStringHelper;

typedef uint8_t byte;

inline unsigned long millis() {
    static unsigned long t = 1;
    return ++t;
}

inline void yield() {}
inline void delay(unsigned long /*ms*/) {}

class Print {
public:
    virtual size_t write(uint8_t) { return 1; }
    virtual size_t write(const uint8_t* /*buf*/, size_t n) { return n; }
    size_t print(const char*) { return 0; }
    size_t print(char) { return 0; }
    size_t print(int, int = DEC) { return 0; }
    size_t print(unsigned int, int = DEC) { return 0; }
    size_t print(long, int = DEC) { return 0; }
    size_t print(unsigned long, int = DEC) { return 0; }
    size_t print(double, int = 2) { return 0; }
    size_t print(unsigned char, int = DEC) { return 0; }
    size_t println() { return 0; }
    size_t println(const char* s) { return print(s); }
    template<typename T> size_t println(T x) { return print(x); }
    template<typename T, typename U> size_t println(T x, U y) { return print(x, y); }
};

class Stream : public Print {
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() { return -1; }
    virtual ~Stream() {}
};
