/**
 * @file db-hash-insert.c
 * Inserting a key-value pair into a DB hash
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Philippe Elie
 */

#include <assert.h>
#include <stdio.h>

#include "db_hash.h"

void db_insert(samples_db_t * hash, db_key_t key, db_value_t value)
{
	size_t index;
	db_index_t new_node;
	db_node_t * node;

	index = hash->hash_base[do_hash(hash, key)];
	while (index) {
		assert(index > 0 && index < hash->descr->current_size);
		node = &hash->node_base[index];
		if (node->key == key) {
			if (node->value + value >= node->value) {
				node->value += value;
			} else {
				/* post profile tools must handle overflow */
				node->value = ~(db_value_t)0;
			}
			return;
		}

		index = node->next;
	}

	/* no locking is necessary: iteration interface retrieve data through
	 * the node_base array, db_hash_add_node() increase current_size but
	 * db_travel just ignore node with a zero key so on setting the key
	 * atomically update the node */
	new_node = db_hash_add_node(hash);

	node = &hash->node_base[new_node];
	node->value = value;
	node->key = key;

	/* we need to recalculate hash code, hash table has perhaps grown */
	index = do_hash(hash, key);
	node->next = hash->hash_base[index];
	hash->hash_base[index] = new_node;
}