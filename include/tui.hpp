#include <meta.hpp>
extern "C" {
#include <os/string.h>
#include <printk.h>
#include <screen.h>
}

template <typename func_t> struct table_entry_t {
    const char* name;
    int len;
    func_t action;
};

template <typename entry_t, typename item_t>
concept is_table_entry = requires(entry_t entry, const item_t* item) {
    { entry.name } -> meta::convertible_to<const char*>;
    { entry.action(item) };
};

template <typename func_t, typename item_t>
concept is_item_filter = requires(func_t func, const item_t* item) {
    { func(item) } -> meta::convertible_to<bool>;
};

template <typename item_t, typename array_t>
concept array_like = requires(array_t array, int i) {
    { array[i] } -> meta::convertible_to<const item_t>;
};

template <typename item_t> void display_header(is_table_entry<item_t> auto&&... entries) {
    ((printkf("%s", entries.name), screen_move_cursor_col(entries.len)), ...);
    printkf("\n");
}

template <typename item_t> void calculate_positions(is_table_entry<item_t> auto&&... entries) {
    int position = 0;
    ((position += entries.len, entries.len = position), ...);
}

template <typename item_t, typename array_t>
    requires array_like<item_t, array_t>
int display_table(
    array_t array, int n, is_item_filter<item_t> auto&& filter,
    is_table_entry<item_t> auto&&... entries) {
    calculate_positions<item_t>(entries...);
    display_header<item_t>(entries...);
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (!filter(&array[i])) continue;
        ((entries.action(&array[i]), screen_move_cursor_col(entries.len)), ...);
        printkf("\n");
        count++;
    }
    screen_reflush();
    return count;
}
