#include "list.h"


//Always Insert at tail
struct node* insert(uint64_t item, node_t** head, node_t** tail)
{
    struct node *ptr = (struct node*) malloc(sizeof(struct node));
    ptr->item = item;
    ptr->next = NULL;
#if SME_DBG
    printf("INSERT: Head %p tail %p item_ptr %p NULL is %p\n", ((node_t*) *head),((node_t*) *tail), ptr, NULL);
#endif
    if (NULL == *head) {
#if SME_DBG
        printf("Head == NULL\n");
#endif
        *head = *tail = ptr;
    } else {
        ((node_t*) *tail)->next = ptr;
        *tail = ptr;
    }
#if SME_DBG
    printf("INSERT: Head %p head_next %p tail %p tail_next %pitem %lu item_ptr %p\n",((node_t*) *head),((node_t*) *head)->next , ((node_t*) *tail),((node_t*) *tail)->next,  ((node_t*) *tail)->item, ptr);
#endif
    return ptr;
}

//Always Remove from Head
uint64_t delete(node_t** head, node_t** tail)
{
    if (NULL == *head) {
        return -1;
    } else {
        struct node *ptr = *head;
        uint64_t item = ptr->item;
        *head = ptr->next;
        if (NULL == *head) {
          *tail = *head; 
#if SME_DBG
          printf("Tail == Head == NULL\n");
#endif
        }
        free(ptr);
        ptr = NULL;
#if SME_DBG
        printf("DELETE: Head %p tail %p item %lu \n",((node_t*) *head), *tail, item);
#endif
        return item;
    }
}

//Read the Head/Tail nodes without deleting
//Usually used to peak on the last inserted value (tail)
uint64_t peak(node_t *node){
    if (NULL == node) {
        return -1;
    } else {
        return node->item;
    }

}

void delete_all(node_t** head, node_t** tail){
    struct node *ptr = *head;
    struct node *ptr_next = *head;

    while (NULL != ptr_next) {
        ptr_next = ptr->next;
        delete(&ptr, tail);
    }
}

void list(node_t** head, node_t** tail)
{
    struct node *ptr = *head;

    while (NULL != ptr) {
        printf("%lu ", ptr->item);
        ptr = ptr->next;
    }

    printf("\n");
}
