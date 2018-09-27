#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct node {
    uint64_t item;
    struct node *next;
} node_t;

struct node* insert(uint64_t item, node_t** head, node_t** tail);
uint64_t delete(node_t** head, node_t** tail);
uint64_t peak(node_t *head);
void delete_all(node_t** head, node_t** tail);
void list(node_t** head, node_t** tail);

