#pragma once

typedef enum {
    BRK_ALWAYS,
    BRK_DEBUG,
} breakpoint_level_t;

// NOTE: default breakpoint level, only higher level breakpoints will be triggered
#ifndef BRK_LEVEL
#define BRK_LEVEL BRK_ALWAYS
#endif

// #define GET_MACRO(_1, _2, NAME, ...) NAME
//
// #define breakpoint(...) GET_MACRO(_, ##__VA_ARGS__, breakpoint1, breakpoint0)(__VA_ARGS__)
//
// #define breakpoint0()      breakpoint_set(BRK_ALWAYS)
// #define breakpoint1(level) breakpoint_set(level)

void breakpoint(void);
void breakpoint_set(breakpoint_level_t level);
