#include <os/list.h>

/**
 * @brief Add a node to the beginning of the list.
 */
void list_prepend(list_head* head, list_node_t* node) {
    node->next = head->next;
    node->prev = head;
    head->next->prev = node;
    head->next = node;
}

/**
 * @brief Add a node to the end of the list.
 */
void list_append(list_head* head, list_node_t* node) {
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
}

/**
 * @brief Pop a node from the beginning of the list.
 */
list_node_t* list_shift(list_head* head) {
    if (head->next == head) {
        return NULL;
    }
    list_node_t* node = head->next;
    head->next = node->next;
    node->next->prev = head;
    return node;
}

/**
 * @brief Pop a node from the end of the list.
 */
list_node_t* list_pop(list_head* head) {
    if (head->prev == head) {
        return NULL;  // List is empty
    }
    list_node_t* node = head->prev;
    head->prev = node->prev;
    node->prev->next = head;
    return node;
}

size_t list_size(const list_head* head) {
    size_t size = 0;
    list_node_t* current = head->next;
    while (current != head) {
        size++;
        current = current->next;
    }
    return size;
}

void list_delete(list_node_t* node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

void list_init(list_head* head) {
    head->next = head;
    head->prev = head;
}
