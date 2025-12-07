#pragma once
extern "C" {
#include <os/mm.h>
#include <pgtable.h>
}

static inline uint64_t pageid(uva_t uva) { return uva >> NORMAL_PAGE_SHIFT; }

// NOTE: uva_object_t: a wrapper for address that may be swapped out
class uva_object_t {
    uva_t addr;

public:
    uva_object_t(uva_t address) : addr(address) {}
    template <typename T> operator T&() {
        asserts(pageid(addr) == pageid(addr + sizeof(T) - 1), "cross-page access");
        alloc_page(addr, current_running->pgdir, true);
        return *reinterpret_cast<T*>(uva2kva(addr, current_running->pgdir));
    }
    template <typename T> T get(size_t index) {
        asserts(
            pageid(addr + index * sizeof(T)) == pageid(addr + (index + 1) * sizeof(T) - 1),
            "cross-page access");
        return *reinterpret_cast<T*>(uva2kva(addr + index * sizeof(T), current_running->pgdir));
    }
    template <typename T> void set(size_t index, T value) {
        asserts(
            pageid(addr + index * sizeof(T)) == pageid(addr + (index + 1) * sizeof(T) - 1),
            "cross-page access");
        *reinterpret_cast<T*>(uva2kva(addr + index * sizeof(T), current_running->pgdir)) = value;
    }
};
