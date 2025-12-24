#pragma once

extern "C" {
#include <os/mm.h>
#include <type.h>
}

template <typename T> struct vector {
    struct iterator {
        T* ptr;
        T& operator*() { return *ptr; }
        const T& operator*() const { return *ptr; }
        iterator& operator++() {
            ptr++;
            return *this;
        }
        iterator operator++(int) {
            iterator temp = *this;
            ptr++;
            return temp;
        }
    };
    vector() : m_capacity(16), m_size(0) { m_data = kmalloc(sizeof(T) * m_capacity); }
    vector(size_t capacity) : m_capacity(capacity), m_size(0) {
        m_data = kmalloc(sizeof(T) * m_capacity);
    }
    ~vector() {
        for (int i = 0; i < m_size; ++i) {
            m_data[i].~T();
        }
        kfree(m_data);
    }

    void push_back(const T& value) {
        if (m_size >= m_capacity) {
            // Resize
            size_t new_capacity = m_capacity * 2;
            T* new_data = kmalloc(sizeof(T) * new_capacity);
            for (size_t i = 0; i < m_size; ++i) {
                new_data[i] = m_data[i];
            }
            kfree(m_data);
            m_data = new_data;
            m_capacity = new_capacity;
        }
        m_data[m_size++] = value;
    }

    size_t size() { return m_size; }

    T& operator[](size_t index) { return m_data[index]; }

    const T& operator[](size_t index) const { return m_data[index]; }

    iterator begin() { return iterator{m_data}; }

    iterator end() { return iterator{m_data + m_size}; }

private:
    T* m_data;
    size_t m_capacity;
    size_t m_size;
};
