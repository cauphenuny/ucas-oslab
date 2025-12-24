#pragma once
#include <meta.hpp>

extern "C" {
#include <type.h>
}

namespace trait {

template <typename T> struct static_polymorphism {
    T& derived() { return *static_cast<T*>(this); }
    const T& derived() const { return *static_cast<const T*>(this); }
};

template <typename T>
concept is_range = requires(T a) {
    { a.size() } -> meta::convertible_to<size_t>;
    { a[0] };
};

template <typename T>
    requires is_range<T>
struct sortable : static_polymorphism<T> {
    void sort() {
        T& d = this->derived();
        size_t n = d.size();
        for (size_t i = 0; i < n - 1; ++i) {
            for (size_t j = 0; j < n - i - 1; ++j) {
                if (d[j] > d[j + 1]) {
                    auto temp = d[j];
                    d[j] = d[j + 1];
                    d[j + 1] = temp;
                }
            }
        }
    }
};

}  // namespace trait
