#ifndef ASSERT_H
#define ASSERT_H

#include <printk.h>

static inline void _panic(const char* file_name,int lineno, const char* func_name)
{
    printk("Assertion failed at %s in %s:%d\n\r",
           func_name,file_name,lineno);
    for(;;);
}

static inline void _panics(const char* file_name, int lineno, const char* func_name, const char* msg)
{
    printk("Assertion failed at %s due to %s in %s:%d\n\r",
           func_name,msg,file_name,lineno);
    for(;;);
}

#define assert(cond)                                 \
    {                                                \
        if (!(cond)) {                               \
            _panic(__FILE__, __LINE__,__FUNCTION__); \
        }                                            \
    }

#define asserts(cond, msg)                                 \
    {                                                      \
        if (!(cond)) {                                     \
            _panics(__FILE__, __LINE__,__FUNCTION__, msg); \
        }                                                  \
    }

#endif /* ASSERT_H */
