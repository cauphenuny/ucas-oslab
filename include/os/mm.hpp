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
        return *reinterpret_cast<T*>(uva2kva(addr, current_running->pgdir));
    }
    template <typename T> T& at(size_t index) {
        asserts(
            pageid(addr + index * sizeof(T)) == pageid(addr + (index + 1) * sizeof(T) - 1),
            "cross-page access");
        return *reinterpret_cast<T*>(uva2kva(addr + index * sizeof(T), current_running->pgdir));
    }
    [[nodiscard]] char* str() {
        if (!addr) return nullptr;
        size_t len = 0;
        while (true) {
            char ch = this->at<char>(len);
            if (ch == '\0') break;
            len++;
        }
        return (char*)addr;  // remain as uva because string may cross pages
        // NOTE: when using c_str(), ensure current_running may not be switched out
    }
    [[nodiscard]] char** argv(int argc) {
        if (!addr) return nullptr;
        for (int i = 0; i < argc; i++) {
            // this->at: ensure each argv[i] is allocated
            // uva_object_t(...).str(): ensure the string that argv[i] points to is allocated
            auto _ = uva_object_t((uva_t)this->at<char*>(i)).str();
        }
        return (char**)(addr);
    }
};
