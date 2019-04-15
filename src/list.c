#include "list.h"

#include "sme_debug.h"

//Always Insert at tail
node_t* insert(uint64_t item, node_t** head, node_t** tail)
{
    node_t *ptr = (node_t*) malloc(sizeof(node_t));
    ptr->item = item;
    ptr->next = NULL;
    wprint(LVL_DBG, "INSERT: Head %p tail %p item_ptr %p NULL is %p"
        , ((node_t*) *head), ((node_t*) *tail), ptr, NULL);
    if (NULL == *head) {
        wprint(LVL_DBG, "%s", "Head == NULL");
        *head = *tail = ptr;
    } else {
        ((node_t*) *tail)->next = ptr;
        *tail = ptr;
    }
    wprint(LVL_DBG, "INSERT: Head %p head_next %p tail %p tail_next %pitem %lu item_ptr %p"
        , ((node_t*) *head), ((node_t*) *head)->next, ((node_t*) *tail)
        , ((node_t*) *tail)->next, ((node_t*) *tail)->item, ptr);
    return ptr;
}

//Always Remove from Head
uint64_t delete(node_t** head, node_t** tail)
{
    if (NULL == *head) {
        return -1;
    } else {
        node_t *ptr = *head;
        uint64_t item = ptr->item;
        *head = ptr->next;
        if (NULL == *head) {
          *tail = *head;
         wprint(LVL_DBG, "%s", "Tail == Head == NULL");
        }
        free(ptr);
        ptr = NULL;
        wprint(LVL_DBG, "DELETE: Head %p tail %p item %lu"
            , ((node_t*) *head), *tail, item);
        return item;
    }
}

//Read the Head/Tail nodes without deleting
//Usually used to peak on the last inserted value (tail)
uint64_t peak(node_t *node){
    if (NULL == node) {
        return 0;
    } else {
        return node->item;
    }

}

void delete_all(node_t** head, node_t** tail){
    node_t *ptr = *head;
    node_t *ptr_next = *head;

    while (NULL != ptr_next) {
        ptr_next = ptr->next;
        delete(&ptr, tail);
    }
}

void list(node_t** head, node_t** tail)
{
    node_t *ptr = *head;

    while (NULL != ptr) {
        printf("%lu ", ptr->item);
        ptr = ptr->next;
    }

    printf("\n");
}
