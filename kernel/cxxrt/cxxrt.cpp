extern "C" {
#include <logger.h>

extern void (*__init_array_start[])();
extern void (*__init_array_end[])();
extern void (*__fini_array_start[])();
extern void (*__fini_array_end[])();

void cxxrt_setup() {
	pretty_logd("calling global constructors...");
    for (auto p = __init_array_start; p < __init_array_end; ++p) {
        (*p)();
    }
}

struct cxa_atexit_entry {
    void (*destructor)(void*);
    void* arg;
    void* dso;
};

static cxa_atexit_entry atexit_list[128];
static size_t atexit_count = 0;

void* __dso_handle = &__dso_handle;

int __cxa_atexit(void (*destructor)(void*), void* arg, void* dso) {
    if (atexit_count >= 128) {
        return -1;
    }

    atexit_list[atexit_count++] = {
        destructor,
        arg,
        dso
    };

    return 0;
}

void __cxa_finalize(void* dso) {
    // reverse order
    for (size_t i = atexit_count; i > 0; --i) {
        auto& e = atexit_list[i - 1];

        if (e.destructor &&
            (dso == nullptr || dso == e.dso)) {

            e.destructor(e.arg);
            e.destructor = nullptr;
        }
    }
}

void cxxrt_teardown() {
	pretty_logd("calling global destructors...");
	__cxa_finalize(nullptr);
    for (auto p = __fini_array_end; p-- > __fini_array_start; ) {
        (*p)();
    }
}

}

