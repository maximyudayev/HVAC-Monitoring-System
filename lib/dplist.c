/**************************************************************************************************************************
 *
 * FileName:        dplist.c
 * Comment:         My Double Linked List Implementation
 * Dependencies:    Header (.h) files if applicable, see below.
 *
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Author                       		    Date                Version             Comment
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Maxim Yudayev	            		    28/10/2019          0.1                 Successful upload to labtools.groept.be
 * 										    30/10/2019			0.2					Implementation of Ex5 functions
 *                                          30/11/2019          0.3                 Several functions updated to remove bugs
 *                                                                                  Added Makefile and separated library
 * 																				    source code for modularity
 *                                          02/12/2019          0.4                 Removed bug and errors from insert_sorted
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * TODO                         		    Date                Finished
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * 1) Implement extra functions			    30/10/2019          30/10/2019
 * 2) Comment code thoroughly               30/10/2019          30/10/2019          Updated on 30/11/2019
 * 3) Consider replacing empty-bodied for's 30/10/2019          -
 * 4) Refactor code, optimize functions     30/10/2019          -
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *************************************************************************************************************************/

/**
 * Includes 
 **/
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "dplist.h"

/**
 * Definition of error codes
 **/
#define DPLIST_NO_ERROR 0
#define DPLIST_MEMORY_ERROR 1 // error due to mem alloc failure
#define DPLIST_INVALID_ERROR 2 //error due to a list operation applied on a NULL list 

#ifdef DEBUG
	#define DEBUG_PRINTF(...) 									         \
		do {											         \
			fprintf(stderr,"\nIn %s - function %s at line %d: ", __FILE__, __func__, __LINE__);	 \
			fprintf(stderr,__VA_ARGS__);								 \
			fflush(stderr);                                                                          \
                } while(0)
#else
	#define DEBUG_PRINTF(...) (void)0
#endif


#define DPLIST_ERR_HANDLER(condition,err_code)\
	do {						            \
            if ((condition)) DEBUG_PRINTF(#condition " failed\n");    \
            assert(!(condition));                                    \
        } while(0)
        
/**
 * Complete Definitions
 **/
struct dplist_node {
    dplist_node_t * prev, * next;
    void * element;
};

struct dplist {
    dplist_node_t * head;
    void * (*element_copy)(void * src_element);			  
    void (*element_free)(void ** element);
    int (*element_compare)(void * x, void * y);
};

/**
 * Functions
 **/
dplist_t * dpl_create ( // callback functions
			  void * (*element_copy)(void * src_element),
			  void (*element_free)(void ** element),
			  int (*element_compare)(void * x, void * y)
              )
{
    dplist_t * list;
    list = malloc(sizeof(dplist_t));
    DPLIST_ERR_HANDLER(list==NULL,DPLIST_MEMORY_ERROR);
    list->head = NULL;  
    list->element_copy = element_copy;
    list->element_free = element_free;
    list->element_compare = element_compare; 
    return list;
}

void dpl_free(dplist_t ** list, bool free_element)
{
    assert(*list != NULL);
    if((*list)->head != NULL)
    {
        while((*list)->head != NULL) dpl_remove_at_index(*list, 0, free_element);
    }
    (*list)->head = NULL;
    (*list)->element_copy = NULL;
    (*list)->element_free = NULL;
    (*list)->element_compare = NULL;
    free(*list);
    (*list) = NULL;
}

dplist_t * dpl_insert_at_index(dplist_t * list, void * element, int index, bool insert_copy)
{
    assert(list != NULL);
    dplist_node_t * dummy = malloc(sizeof(dplist_node_t));
    if(index <= 0)
    {
        if(list->head == NULL)
        {
            list->head = dummy;
            dummy->next = NULL;
        } else
        {
            list->head->prev = dummy;
            dummy->next = list->head;
            list->head = dummy;
        }
        dummy->prev = NULL;
    } else {
        dplist_node_t * dummy_old = dpl_get_reference_at_index(list, index);
        assert(dummy_old != NULL);
        if(index >= dpl_size(list))
        {
            dummy_old->next = dummy;
            dummy->prev = dummy_old;
            dummy->next = NULL;
        } else
        {
            dummy->next = dummy_old;
            dummy->prev = dummy_old->prev;
            dummy_old->prev->next = dummy;
            dummy_old->prev = dummy;
        }
    }
    if(insert_copy == true)
    {
        dummy->element = (list->element_copy)(element);
    } else {
        dummy->element = element;
    }
    return list;
}

dplist_t * dpl_remove_at_index(dplist_t * list, int index, bool free_element)
{
    assert(list != NULL);
    if(list->head == NULL) return list;
    dplist_node_t * dummy = list->head;
    if(index <= 0)
    {
        if(free_element == true) (list->element_free)(&(list->head->element));
        if(list->head->next == NULL)
        {
            list->head = NULL;
        } else 
        {
            dummy->next->prev = NULL;
            list->head = dummy->next;
        }
    } else
    {
        int id = 0;
        while(dummy->next != NULL && id < index) dummy = dummy->next, id++;
        if(free_element == true) (list->element_free)(&(dummy->element));
        if(dummy == list->head)
        {
            list->head = NULL;
        } else
        {
            if(dummy->next == NULL)
            {
                dummy->prev->next = NULL;
            } else
            {
                dummy->prev->next = dummy->next;
                dummy->next->prev = dummy->prev;
            }
        }
    }
    dummy->prev = dummy->next = dummy->element = NULL;
    free(dummy);
    return list;
}

int dpl_size(dplist_t * list)
{
    assert(list != NULL);
    if(list->head == NULL) return 0;
    int size = 1;
    dplist_node_t * dummy = list->head;
    while(dummy->next != NULL) dummy = dummy->next, size++;
    return size;
}

void * dpl_get_element_at_index(dplist_t * list, int index)
{
    assert(list != NULL);
    if(list->head == NULL) return (void *) 0;
    if(index <= 0) return (void *) (list->head->element);
    dplist_node_t * dummy = list->head;
    int id = 0;
    while(dummy->next != NULL && id < index) dummy = dummy->next, id++;
    return (void *) (dummy->element);
}

int dpl_get_index_of_element(dplist_t * list, void * element)
{
    assert(list != NULL);
    if(list->head == NULL) return -1;
    dplist_node_t * dummy = list->head;
    int id = 0;
    while(dummy->next != NULL && (list->element_compare)(element, dummy->element) != 0) // Goes through the list until the end or until the user specified compare function finds member with equal element
    {
        dummy = dummy->next;
        id++;
    }
    if((list->element_compare)(element, dummy->element) == 0) return id; // If a member with equalling element is found, returns its index, otherwise returns -1
	return -1; 
}

dplist_node_t * dpl_get_reference_at_index(dplist_t * list, int index)
{
    assert(list != NULL);
    if(list->head == NULL) return NULL;
    if(index <= 0) return list->head;
    dplist_node_t * dummy = list->head;
    int id = 0;
    while(dummy->next != NULL && id < index) dummy = dummy->next, id++; // Goes through the list until the end or the specified location
    return dummy; // Returns the last applicable indexed member
}
 
void * dpl_get_element_at_reference(dplist_t * list, dplist_node_t * reference)
{
    assert(list != NULL);
    if(list->head == NULL) return NULL;
    dplist_node_t * dummy = list->head;
    if(reference == NULL)
    {
        while(dummy->next != NULL) dummy = dummy->next; // Gets the last member of the list and returns its element's pointer
        return dummy->element;
    } else
    {
        while(dummy->next != NULL && dummy != reference) dummy = dummy->next; // Goes through the list until the end or until the reference is found
        if(dummy == reference) return dummy->element; // If reference is found, return its element's data
        return NULL; // If end is reached and reference not found, return NULL
    }
}

void * dpl_get_element_of_reference(dplist_node_t * reference)
{
    assert(reference != NULL);
    return reference->element;
}

dplist_node_t * dpl_get_first_reference(dplist_t * list)
{
    return dpl_get_reference_at_index(list, 0); // Implementation takes care of error handling and logic
}

dplist_node_t * dpl_get_last_reference(dplist_t * list)
{
    return dpl_get_reference_at_index(list, dpl_size(list)-1); // Implementation takes care of error handling and logic
}

dplist_node_t * dpl_get_next_reference(dplist_t * list, dplist_node_t * reference)
{
    dplist_node_t * dummy = dpl_get_reference_if_member(list, reference);
    if(dummy == NULL) return NULL; // // Will return NULL if reference is not a member of the list or list is empty
    return dummy->next; // Will return NULL if reference is not a member of the list, if reference is the last member of the list; Will return member right after reference otherwise
}

dplist_node_t * dpl_get_previous_reference(dplist_t * list, dplist_node_t * reference)
{
    dplist_node_t * dummy = dpl_get_reference_if_member(list, reference);
    if(dummy == NULL) return NULL; // // Will return NULL if reference is not a member of the list or list is empty
    return dummy->prev; // Will return NULL if reference is not a member of the list, if reference is the first member of the list; Will return member right before reference otherwise
}

dplist_node_t * dpl_get_reference_if_member(dplist_t * list, dplist_node_t * reference)
{
    assert(list != NULL);
    if(list->head == NULL || reference == NULL) return NULL;
    dplist_node_t * dummy;
    for(dummy = list->head; dummy->next != NULL && reference != dummy; dummy = dummy->next); // Goes through the list until end is reached or the element is found
    return (dummy->next == NULL && reference != dummy)? NULL : dummy; // If reference not in list, returns NULL. Otherwise returns matching node
}

dplist_node_t * dpl_get_reference_of_element(dplist_t * list, void * element)
{
    assert(list != NULL);
    if(list->head == NULL) return NULL;
    dplist_node_t * dummy = list->head;
    while(dummy->next != NULL && (list->element_compare)(element, dummy->element) != 0) // Goes through the list until end or until finds an equal member or end
    {
        dummy = dummy->next;
    }
    if((list->element_compare)(element, dummy->element) == 0) return dummy; // If matching member was found, return the found member
    return NULL; // Otherwise return NULL
}

int dpl_get_index_of_reference(dplist_t * list, dplist_node_t * reference)
{
    return dpl_get_index_of_element(list, reference->element); // Implementation takes care of error handling and logic
}

dplist_t * dpl_insert_at_reference(dplist_t * list, void * element, dplist_node_t * reference, bool insert_copy)
{
    assert(list != NULL);
    dplist_node_t * dummy;
    if(reference == NULL) 
    {
        dummy = dpl_get_last_reference(list); // NULL or a list member
    }
    else if(list->head != NULL && reference != NULL)
    {
        for(dummy = list->head; dummy->next != NULL && dummy != reference; dummy = dummy->next); // Goes through the list until end is reached or until finds a member pointing to the same dplist_node
        if(dummy->next == NULL && dummy != reference) return list; // Returns unmodified list if reference is not an existing member of the list
    } 
    else 
    {
        return list; // If list is empty, but reference non-NULL, the member is not an existing member, returns unmodified list
    }

    if(insert_copy == true) {
        element = (list->element_copy)(element); // If element to copy
    }
    dplist_node_t * new_node = malloc(sizeof(dplist_node_t)); // Creates new node
    new_node->element = element; // New node points to copied element if insert_copy=true or to original if insert_copy=false
    if(dummy == NULL) // List is empty
    {
        list->head = new_node;
        new_node->prev = new_node->next = NULL;
    } else
    {
        if(reference == NULL) // Dummy member is the last in the list
        {
            dummy->next = new_node;
            new_node->prev = dummy;
            new_node->next = NULL;
        } else if(dummy->prev == NULL) // Dummy is the first in the list
        {
            list->head = new_node;
            new_node->next = dummy;
            new_node->prev = NULL;
            dummy->prev = new_node;
        } else // Dummy is anywhere in between first and last members of the list
        {
            new_node->next = dummy;
            new_node->prev = dummy->prev;
            dummy->prev->next = new_node;
            dummy->prev = new_node;
        } 
    }
    return list;
}

dplist_t * dpl_insert_sorted(dplist_t * list, void * element, bool insert_copy)
{
    assert(list != NULL);
    if(insert_copy == true) element = (list->element_copy)(element); // If element to copy
    dplist_node_t * new_node = (dplist_node_t *) malloc(sizeof(dplist_node_t));
    new_node->element = element;
    if(list->head == NULL) // If list is empty
    {
        list->head = new_node;
        new_node->prev = new_node->next = NULL;
    } else
    {
        dplist_node_t * dummy = list->head;
        while(dummy->next != NULL && (list->element_compare)(new_node->element, dummy->element) > 0) // Goes through the collection until the end or until finds a member equal or larger than new_node
        {
            dummy = dummy->next;
        }
        if(dummy->next == NULL && (list->element_compare)(new_node->element, dummy->element) >= 0) // If new_node larger than all members of the list or is equal to last, append it to the end of list
        {
            new_node->next = NULL;
            new_node->prev = dummy;
            dummy->next = new_node;
        } else if(dummy == list->head && (list->element_compare)(new_node->element, dummy->element) < 0) // If new_node is the smallest in list
        {
            list->head = new_node;
            new_node->prev = NULL;
            new_node->next = dummy;
            dummy->prev = new_node;
        } else // If new_node is anywhere in the middle of list
        {
            new_node->next = dummy;
            new_node->prev = dummy->prev;
            dummy->prev->next = new_node;
            dummy->prev = new_node;
        }
    }
    return list;
}

dplist_t * dpl_remove_at_reference(dplist_t * list, dplist_node_t * reference, bool free_element)
{
    assert(list != NULL);
    if(list->head == NULL) return list;
    if(reference == NULL) return dpl_remove_at_index(list, dpl_size(list)-1, free_element);
    dplist_node_t * dummy;
    for(dummy = list->head; dummy->next != NULL && dummy != reference; dummy = dummy->next);
    if(dummy->next == NULL && dummy != reference) return list;
    return dpl_remove_node(list, dummy, free_element); // Implementation takes care of freeing
}

dplist_t * dpl_remove_element(dplist_t * list, void * element, bool free_element)
{
    assert(list != NULL);
    if(list->head == NULL) return list;
    dplist_node_t * dummy;
    for(dummy = list->head; dummy->next != NULL && (list->element_compare)(dummy->element, element) != 0; dummy = dummy->next); // Goes through list until end or until finds a member of equal element
    if(dummy->next == NULL && dummy->element != element) return list; // No member pointing to 'element' was found in list
    return dpl_remove_node(list, dummy, free_element); // Implementation takes care of freeing
}

dplist_t * dpl_remove_node(dplist_t * list, dplist_node_t * dummy, bool free_element)
{
    assert(list != NULL && dummy != NULL);
    if(dummy == list->head) // If element to remove is first in the list
    {
        list->head = dummy->next;
        if(dummy->next != NULL) dummy->next->prev = NULL; // If list is larger than 1 element
    } else // If element is anywhere else in list
    {
        dummy->prev->next = dummy->next; // Applies to dummy in the middle and at the end of list
        if(dummy->next != NULL) dummy->next->prev = dummy->prev; // Applies to dummy not at the end of list
    }
    if(free_element == true) (list->element_free)(&(dummy->element)); // If element to free
    dummy->next = dummy->prev = NULL;
    dummy->element = NULL;
    free(dummy);
    return list;
}

void dpl_print_heap(dplist_t * list)
{
    if(list == NULL)
    {
        printf("List no longer exists\n"); 
        return;
    }
    if(list->head == NULL)
    { 
        printf("List at %p is empty\n", list); 
        return;
    }
    dplist_node_t * dummy;
    printf("Array || Element || Previous || Next || Data\n");
    for(dummy = list->head; dummy->next != NULL; dummy = dummy->next)
    {
        printf("%p || %p || %p || %p || %p\n", list, dummy, dummy->prev, dummy->next, dummy->element);
    }
    printf("%p || %p || %p || %p || %p\n----------------\n", list, dummy, dummy->prev, dummy->next, dummy->element);
}