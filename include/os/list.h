/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * * Copyright (C) 2018 Institute of Computing
 * Technology, CAS Author : Han Shukai (email :
 * hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * * Changelog: 2019-8 Reimplement queue.h.
 * Provide Linux-style doube-linked list instead of original
 * unextendable Queue implementation. Luming
 * Wang(wangluming@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * * * * * * * * * * * */

#ifndef INCLUDE_LIST_H_
#define INCLUDE_LIST_H_

#include <type.h>

struct list;

// double-linked list
typedef struct list_node {
    struct list_node *next, *prev;
    struct list* container;
} list_node_t;

typedef struct list {
    list_node_t head;
    const char* name;
} list_t;

// LIST_HEAD is used to define the head of a list.
#define LIST(ident, name) list_t ident = {{&((ident).head), &((ident).head)}, name}

#define container_of(ptr, type, member)                       \
    ({                                                        \
        const typeof(((type*)0)->member)* __mptr = (ptr);     \
        (type*)((char*)__mptr - (size_t)&((type*)0)->member); \
    })

/* TODO: [p2-task1] implement your own list API */

void list_init(list_t* head, const char* name);

/**
 * @brief Add a node to the beginning of the list.
 */
void list_prepend(list_t* head, list_node_t* node);
/**
 * @brief Add a node to the end of the list.
 */
void list_append(list_t* head, list_node_t* node);
/**
 * @brief Pop a node from the beginning of the list.
 */
list_node_t* list_shift(list_t* head);
/**
 * @brief Pop a node from the end of the list.
 */
list_node_t* list_pop(list_t* head);

bool list_holding(list_node_t* node);

void list_delete(list_node_t* node);

void list_node_destruct(list_node_t* node);

size_t list_size(const list_t* head);

void list_traverse(list_t* head, void (*func)(list_node_t* node));

#define list_foreach_item(iter, head, type, member)                                             \
    for (type* iter = head->next == head ? NULL : container_of(head->next, type, member); iter; \
         iter = iter->member->next == head ? NULL                                               \
                                           : container_of(iter->member->next, type, member))

#define list_foreach_node(iter, head)                                               \
    for (list_node_t* iter = (head)->next, *iter_next = iter->next; iter != (head); \
         iter = iter_next, iter_next = iter->next)

#define list_foreach_node_reversed(iter, head)

#endif
