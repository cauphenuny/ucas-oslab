#include <assert.h>
#include <logger.h>
#include <os/list.h>

/**
 * @brief Add a node to the beginning of the list.
 */
void list_prepend(list_t* list, list_node_t* node) {
    if (list_holding(node)) {
        pretty_loge("trying to prepend a node already in list %s: %x", list->name, node);
        return;
    }
    list_node_t* head = &list->head;
    node->next = head->next;
    node->prev = head;
    head->next->prev = node;
    head->next = node;
    node->container = list;
}

/**
 * @brief Add a node to the end of the list.
 */
void list_append(list_t* list, list_node_t* node) {
    if (list_holding(node)) {
        pretty_loge("trying to append a node already in list %s: %x", list->name, node);
        return;
    }
    list_node_t* head = &list->head;
    node->prev = head->prev;
    node->next = head;
    head->prev->next = node;
    head->prev = node;
    node->container = list;
}

/**
 * @brief Pop a node from the beginning of the list.
 */
list_node_t* list_shift(list_t* list) {
    list_node_t* head = &list->head;
    if (head->next == head) {
        return NULL;
    }
    list_node_t* node = head->next;
    head->next = node->next;
    node->next->prev = head;
    node->next = node->prev = NULL;
    node->container = NULL;
    return node;
}

/**
 * @brief Pop a node from the end of the list.
 */
list_node_t* list_pop(list_t* list) {
    list_node_t* head = &list->head;
    if (head->prev == head) {
        return NULL;  // List is empty
    }
    list_node_t* node = head->prev;
    head->prev = node->prev;
    node->prev->next = head;
    node->next = node->prev = NULL;
    node->container = NULL;
    return node;
}

size_t list_size(const list_t* list) {
    const list_node_t* head = &list->head;
    size_t size = 0;
    list_node_t* current = head->next;
    while (current != head) {
        size++;
        current = current->next;
    }
    return size;
}

void list_delete(list_node_t* node) {
    asserts(list_holding(node), "trying to delete a node not in any list");
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = node->next = NULL;
    node->container = NULL;
}

void list_init(list_t* list, const char* name) {
    list_node_t* head = &list->head;
    head->next = head;
    head->prev = head;
    list->name = name;
}

bool list_holding(list_node_t* node) {
    asserts(!((node->next != NULL) ^ (node->prev != NULL)), "node link broken");
    asserts(!((node->next != NULL) ^ (node->container != NULL)), "node container broken");
    return node->next != NULL || node->prev != NULL;
}

void list_node_destruct(list_node_t* node) {
    if (list_holding(node)) {
        list_delete(node);
    }
}