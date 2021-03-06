/*
 * kmscon - Miscellaneous Helpers
 *
 * Copyright (c) 2011 David Herrmann <dh.herrmann@googlemail.com>
 * Copyright (c) 2011 University of Tuebingen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Miscellaneous Helpers
 * Rings: Rings are used to buffer a byte-stream of data. It works like a FIFO
 * queue but in-memory.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "misc.h"

#define LOG_SUBSYSTEM "misc"

#define RING_SIZE 512

struct ring_entry {
	struct ring_entry *next;
	size_t len;
	char buf[];
};

struct kmscon_ring {
	struct ring_entry *first;
	struct ring_entry *last;
};

int kmscon_ring_new(struct kmscon_ring **out)
{
	struct kmscon_ring *ring;

	if (!out)
		return -EINVAL;

	ring = malloc(sizeof(*ring));
	if (!ring)
		return -ENOMEM;

	memset(ring, 0, sizeof(*ring));

	*out = ring;
	return 0;
}

void kmscon_ring_free(struct kmscon_ring *ring)
{
	struct ring_entry *tmp;

	if (!ring)
		return;

	while (ring->first) {
		tmp = ring->first;
		ring->first = tmp->next;
		free(tmp);
	}
	free(ring);
}

bool kmscon_ring_is_empty(struct kmscon_ring *ring)
{
	if (!ring)
		return true;

	return ring->first == NULL;
}

int kmscon_ring_write(struct kmscon_ring *ring, const char *val, size_t len)
{
	struct ring_entry *ent;
	size_t space, cp;

	if (!ring || !val || !len)
		return -EINVAL;

next:
	ent = ring->last;
	if (!ent || ent->len >= RING_SIZE) {
		ent = malloc(sizeof(*ent) + RING_SIZE);
		if (!ent)
			return -ENOMEM;

		ent->len = 0;
		ent->next = NULL;
		if (ring->last)
			ring->last->next = ent;
		else
			ring->first = ent;
		ring->last = ent;
	}

	space = RING_SIZE - ent->len;
	if (len >= space)
		cp = space;
	else
		cp = len;

	memcpy(&ent->buf[ent->len], val, cp);
	ent->len += cp;

	val = &val[cp];
	len -= cp;
	if (len > 0)
		goto next;

	return 0;
}

const char *kmscon_ring_peek(struct kmscon_ring *ring, size_t *len)
{
	if (!ring || !ring->first)
		return NULL;

	*len = ring->first->len;
	return ring->first->buf;
}

void kmscon_ring_drop(struct kmscon_ring *ring, size_t len)
{
	struct ring_entry *ent;

	if (!ring || !len)
		return;

next:
	ent = ring->first;
	if (!ent)
		return;

	if (len >= ent->len) {
		len -= ent->len;
		ring->first = ent->next;
		free(ent);
		if (!ring->first)
			ring->last = NULL;

		if (len)
			goto next;
	} else {
		memmove(ent->buf, &ent->buf[len], ent->len - len);
		ent->len -= len;
	}
}

struct hook_entry {
	struct hook_entry *next;
	kmscon_hook_cb cb;
	void *data;
};

struct kmscon_hook {
	unsigned int num;
	struct hook_entry *entries;
	struct hook_entry *cur_entry;
};

int kmscon_hook_new(struct kmscon_hook **out)
{
	struct kmscon_hook *hook;

	if (!out)
		return -EINVAL;

	hook = malloc(sizeof(*hook));
	if (!hook)
		return -ENOMEM;
	memset(hook, 0, sizeof(*hook));

	*out = hook;
	return 0;
}

void kmscon_hook_free(struct kmscon_hook *hook)
{
	struct hook_entry *entry;

	if (!hook)
		return;

	while ((entry = hook->entries)) {
		hook->entries = entry->next;
		free(entry);
	}

	free(hook);
}

unsigned int kmscon_hook_num(struct kmscon_hook *hook)
{
	if (!hook)
		return 0;

	return hook->num;
}

int kmscon_hook_add(struct kmscon_hook *hook, kmscon_hook_cb cb, void *data)
{
	struct hook_entry *entry;

	if (!hook || !cb)
		return -EINVAL;

	entry = malloc(sizeof(*entry));
	if (!entry)
		return -ENOMEM;
	memset(entry, 0, sizeof(*entry));
	entry->cb = cb;
	entry->data = data;

	entry->next = hook->entries;
	hook->entries = entry;
	hook->num++;
	return 0;
}

void kmscon_hook_rm(struct kmscon_hook *hook, kmscon_hook_cb cb, void *data)
{
	struct hook_entry *entry, *tmp;

	if (!hook || !cb || !hook->entries)
		return;

	tmp = NULL;
	if (hook->entries->cb == cb && hook->entries->data == data) {
		tmp = hook->entries;
		hook->entries = tmp->next;
	} else for (entry = hook->entries; entry->next; entry = entry->next) {
		if (entry->next->cb == cb && entry->next->data == data) {
			tmp = entry->next;
			entry->next = tmp->next;
			break;
		}
	}

	if (tmp) {
		/* if *_call() is currently running we must not disturb it */
		if (hook->cur_entry == tmp)
			hook->cur_entry = tmp->next;
		free(tmp);
		hook->num--;
	}
}

void kmscon_hook_call(struct kmscon_hook *hook, void *parent, void *arg)
{
	if (!hook)
		return;

	hook->cur_entry = hook->entries;
	while (hook->cur_entry) {
		hook->cur_entry->cb(parent, arg, hook->cur_entry->data);
		if (hook->cur_entry)
			hook->cur_entry = hook->cur_entry->next;
	}
}

/* Hash Tables
 * This is currently a wrapper around the GHashTable glib object. We should
 * replace this with an own hash-table to avoid glib dependency. However, I
 * currently don't have the time so we use glib.
 * TODO: Write an own hash-table implementation!
 */

#include <glib.h>

struct kmscon_hashtable {
	GHashTable *tbl;
};

unsigned int kmscon_direct_hash(const void *data)
{
	return g_direct_hash(data);
}

int kmscon_direct_equal(const void *data1, const void *data2)
{
	return g_direct_equal(data1, data2);
}

int kmscon_hashtable_new(struct kmscon_hashtable **out,
				kmscon_hash_cb hash_cb,
				kmscon_equal_cb equal_cb,
				kmscon_free_cb free_key,
				kmscon_free_cb free_value)
{
	struct kmscon_hashtable *tbl;

	if (!out || !hash_cb || !equal_cb)
		return -EINVAL;

	tbl = malloc(sizeof(*tbl));
	if (!tbl)
		return -ENOMEM;
	memset(tbl, 0, sizeof(*tbl));

	tbl->tbl = g_hash_table_new_full(hash_cb, equal_cb,
						free_key, free_value);
	if (!tbl->tbl) {
		log_err("cannot allocate GHashTable");
		free(tbl);
		return -ENOMEM;
	}

	*out = tbl;
	return 0;
}

void kmscon_hashtable_free(struct kmscon_hashtable *tbl)
{
	if (!tbl)
		return;

	g_hash_table_unref(tbl->tbl);
}

int kmscon_hashtable_insert(struct kmscon_hashtable *tbl, void *key,
				void *data)
{
	if (!tbl)
		return -EINVAL;

	g_hash_table_insert(tbl->tbl, key, data);
	return 0;
}

bool kmscon_hashtable_find(struct kmscon_hashtable *tbl, void **out, void *key)
{
	void *val;
	gboolean res;

	if (!tbl)
		return -EINVAL;

	res = g_hash_table_lookup_extended(tbl->tbl, key, NULL, &val);
	if (res != TRUE)
		return false;

	if (out)
		*out = val;
	return true;
}
