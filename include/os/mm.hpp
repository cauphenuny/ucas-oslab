#pragma once
extern "C" {
#include <os/mm.h>
#include <pgtable.h>
}

// NOTE: suva_t: a wrapper for address that may be swapped out
class suva_t {
    uva_t addr;

public:
    suva_t(uva_t address) : addr(address) {}
    void reserve(size_t nbytes) {
        size_t npages = (nbytes + PAGE_SIZE - 1) / PAGE_SIZE;
        for (size_t i = 0; i < npages; i++) {
            alloc_page(addr + i * PAGE_SIZE, current_running->pgdir, true);
        }
    }
    template <typename T> T get(size_t index) {
        uva_t target = addr + index * sizeof(T);
        PTE* pte = alloc_page(target, current_running->pgdir, true);
        asserts(get_attribute(*pte, _PAGE_PRESENT), "page not present");
        return *((T*)pa2kva(get_pa(*pte)));
    }
    template <typename T> void set(size_t index, T value) {
        uva_t target = addr + index * sizeof(T);
        PTE* pte = alloc_page(target, current_running->pgdir, true);
        asserts(get_attribute(*pte, _PAGE_PRESENT), "page not present");
        *((T*)pa2kva(get_pa(*pte))) = value;
    }
    operator void*() {
        alloc_page(addr, current_running->pgdir, true);
        return (void*)addr;
    }
    operator kva_t() {
        alloc_page(addr, current_running->pgdir, true);
        return addr;
    }
};
