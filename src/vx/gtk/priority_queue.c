#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "priority_queue.h"


//this implements a priority queue as a binary heap for a*

priority_queue_t* heap_create()
{
	priority_queue_t* pq = calloc(1, sizeof(priority_queue_t));

	return pq;
}


int heap_left_child(int i)
{
	return 2*i + 1;
}

int heap_right_child(int i)
{
	return 2*i + 2;
}

int heap_parent(int i)
{
	return (i-1)/2;
}

int node_comp(node_t* n1, node_t* n2){
    return n1->rank > n2->rank; 
}

void fix_down_heap(priority_queue_t* pq, int i)
{
	int l = heap_left_child(i);
	int r = heap_right_child(i);
	int largest;
	int size = pq->num_elts;
	
	if(l < pq->num_elts && node_comp(pq->heap + i, pq->heap + l)){
		largest = l;
	}

	else largest = i;

	if(r < size && node_comp(pq->heap + largest,pq->heap + r)){
		largest = r;
	}

	
	
	if(largest != i){
		
		node_t tmp = pq->heap[i];
		
		pq->heap[i] = pq->heap[largest];
		pq->heap[largest] = tmp;

		fix_down_heap(pq, largest);
	}

}

void fix_up_heap(priority_queue_t* pq, int i)
{
	int parent = heap_parent(i);
	
	if(node_comp(pq->heap + parent, pq->heap + i)){
		
		node_t tmp = pq->heap[i];

		pq->heap[i] = pq->heap[parent];
		pq->heap[parent] = tmp;

		fix_up_heap(pq, parent); 
	}
}

void build_heap(priority_queue_t* pq)
{
	int size = pq->num_elts;
	for(int i = size/2 -1; i >= 0; --i){
		fix_down_heap(pq, i);
	}
}

void heap_push(priority_queue_t* pq, node_t* elt)
{
	pq->heap[pq->num_elts++] = *elt;
	fix_up_heap(pq, pq->num_elts -1);
}

void heap_pop(priority_queue_t* pq)
{
	pq->heap[0] = pq->heap[pq->num_elts - 1];
	--pq->num_elts;
	fix_down_heap(pq, 0);
}

node_t heap_top(priority_queue_t* pq)
{
	return pq->heap[0];
}

int heap_empty(priority_queue_t* pq)
{
	return pq->num_elts == 0;
}

int heap_query(priority_queue_t* pq, node_t* node)
{
	for(int i = 0; i < pq->num_elts; ++i){
	
		if(pq->heap[i].x == node->x && pq->heap[i].y == node->y) return i;
	
	}

	return -1;
}

void heap_decrease_key(priority_queue_t* pq, int node_pos, double new_rank)
{
	pq->heap[node_pos].rank = new_rank;
	fix_up_heap(pq, node_pos);
}

void heap_remove_elt(priority_queue_t* pq, int node_pos){
	--pq->num_elts;
	pq->heap[node_pos] = pq->heap[pq->num_elts];
	fix_down_heap(pq, node_pos);
}

void print_heap(priority_queue_t* pq){
	printf("HEAP : ");
	for(int j = 0; j < pq->num_elts; ++j){
		printf("[%f]", pq->heap[j].rank);		
	}
	printf("\n\n");
}

void heap_test(){
	// priority_queue_t* pq = heap_create();

	// node_t node;
	// node.rank = 20;

	// heap_push(pq, &node);

	// node.rank = 10;
	// heap_push(pq, &node);
	
	// for(int i = 0; i < 10; ++i){
	// 	node.rank = i;
	// 	heap_push(pq, &node);
	// 	//heap_push(za, &node);
	// }
	
	// node.rank = 30;
	// heap_push(pq, &node);

	// node.rank = 2;
	// heap_push(pq, &node);


	// node_t* top;
	// int size = pq->num_elts;
	// print_heap(pq);
	// for(int i = 0; i < size; ++i){
	// 	top = heap_top(pq);
	// 	printf("top rank = %f\n", top->rank);
	// 	heap_pop(pq);
	// }

}
 