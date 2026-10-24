/**
 *
 * cpulimit - a CPU limiter for Linux
 *
 * Copyright (C) 2005-2012 Angelo Marletta
 * Copyright (C) 2016-2017, 2019, 2026 Denys Denysov
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdlib.h>
#include <string.h>

#include "core/alloc.h"
#include "core/list.h"

#define EMPTYLIST NULL

void init_list(struct list *l,int keysize) {
    l->first=l->last=NULL;
    l->keysize=keysize;
    l->count=0;
}

struct list_node *add_elem(struct list *l,void *elem) {
    struct list_node *newnode=(struct list_node *)CL_MALLOC(sizeof(struct list_node));
    if (newnode==NULL) {
        return NULL;
    }
    newnode->data=elem;
    newnode->previous=l->last;
    newnode->next=NULL;
    if (l->count==0) {
        l->first=l->last=newnode;
    } else {
        l->last->next=newnode;
        l->last=newnode;
    }
    l->count++;
    return newnode;
}

void delete_node(struct list *l,struct list_node *node) {
    if (l->count==1) {
        l->first=l->last=NULL;
    } else if (node==l->first) {
        node->next->previous=NULL;
        l->first=node->next;
    } else if (node==l->last) {
        node->previous->next=NULL;
        l->last=node->previous;
    } else {
        node->previous->next=node->next;
        node->next->previous=node->previous;
    }
    l->count--;
    CL_FREE(node);
}

void destroy_node(struct list *l,struct list_node *node) {
    CL_FREE(node->data);
    node->data=NULL;
    delete_node(l,node);
}

int is_empty_list(struct list *l) {
    return (l->count==0?TRUE:FALSE);
}

int get_list_count(struct list *l) {
    return l->count;
}

void *first_elem(struct list *l) {
    return l->first==NULL?NULL:l->first->data;
}

struct list_node *first_node(struct list *l) {
    return l->first;
}

void *last_elem(struct list *l) {
    return l->last==NULL?NULL:l->last->data;
}

struct list_node *last_node(struct list *l) {
    return l->last;
}

struct list_node *xlocate_node(struct list *l,void *elem,int offset,int length) {
    struct list_node *tmp;
    tmp=l->first;
    while (tmp!=NULL) {
        if (!memcmp((char *)tmp->data+offset,elem,length==0?l->keysize:length)) {
            return (tmp);
        }
        tmp=tmp->next;
    }
    return EMPTYLIST;
}

struct list_node *locate_node(struct list *l,void *elem) {
    return (xlocate_node(l,elem,0,0));
}

void *xlocate_elem(struct list *l,void *elem,int offset,int length) {
    struct list_node *node=xlocate_node(l,elem,offset,length);
    return (node==NULL?NULL:node->data);
}

void *locate_elem(struct list *l,void *elem) {
    return (xlocate_elem(l,elem,0,0));
}

void clear_list(struct list *l) {
    while (l->first!=EMPTYLIST) {
        struct list_node *tmp;
        tmp=l->first;
        l->first=l->first->next;
        CL_FREE(tmp);
        tmp=NULL;
    }
    l->last=EMPTYLIST;
    l->count=0;
}

void destroy_list(struct list *l) {
    while (l->first!=EMPTYLIST) {
        struct list_node *tmp;
        tmp=l->first;
        l->first=l->first->next;
        CL_FREE(tmp->data);
        tmp->data=NULL;
        CL_FREE(tmp);
        tmp=NULL;
    }
    l->last=EMPTYLIST;
    l->count=0;
}
