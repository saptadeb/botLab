#ifndef __PRIORITY_QUEUE_H__
#define __PRIORITY_QUEUE_H__

#include <stddef.h>

#endif


typedef struct node node_t;
typedef struct priority_queue priority_queue_t;

struct node
{
    //velocity_cmd_t pos;
    int x;
    int y;
    double cost;
    double rank;
    node_t* parent;
};

struct priority_queue
{
	node_t heap[5000];
	int num_elts;
};

priority_queue_t* heap_create();

int heap_left_child(int i);

int heap_right_child(int i);

int heap_parent(int i);

int node_comp(node_t* n1, node_t* n2);

void fix_down_heap(priority_queue_t* pq, int i);

void fix_up_heap(priority_queue_t* pq, int i);

void build_heap(priority_queue_t* pq);

void heap_push(priority_queue_t* pq, node_t* elt);

void heap_pop(priority_queue_t* pq);

node_t heap_top(priority_queue_t* pq);

int heap_empty(priority_queue_t* pq);

int heap_query(priority_queue_t* pq, node_t* node);

void heap_decrease_key(priority_queue_t* pq, int node_pos, double new_rank);

void heap_remove_elt(priority_queue_t* pq, int node_pos);

void print_heap(priority_queue_t* pq);

void heap_test();
