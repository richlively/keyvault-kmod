/* Author:  Keith Shomper
 * Date:    8 Nov 2016
 * Purpose: Supports HW4 for CS3320 */

#include <linux/slab.h>       /* for kmalloc*/
#include <linux/string.h>     /* for memset*/
#include "key_vault.h"

/* the key vault:  once globally available, but now specified by a parameter */
// static struct key_vault v;

/* init_vault:  initializes the key vault */
int  init_vault (struct key_vault *v, int size) {

   /* allocate memory for the key vault */
   v->num_users = 0;
   v->ukey_data = kmalloc(size*sizeof(struct kv_list_h), GFP_KERNEL);
	memset(v->ukey_data, 0, size*sizeof(struct kv_list_h));

   /* if error with allocation, return FALSE */
   if (v->ukey_data == NULL) return FALSE;

   /* otherwise, set the num_users field accordingly */
   v->num_users = size;
   return TRUE;
}

/* dump_vault:  prints the contents of the vault to stdout */
void dump_vault (struct key_vault *v, int dir) {
   struct kv_list_h *udata = v->ukey_data;
	seq_func_ptr      next  = (dir == FORWARD) ? next_key : prev_key;
	int               num   = v->num_users;
	int               u;
   
   /* print the keys for each user */
   for (u = 0; u < num; u++) {
   	int  uid  = (dir == FORWARD) ? u : num-u-1; /* uid is zero-indexed */
      int  n    = udata[uid].num_keys;
      int  i;

      // printf("Key-value pairs for user %d:\n", uid+1);

		/* this user has no keys to print */
		if (n == 0) continue;

		/* references the key-value pair to be printed */
      struct kv_list *l;

		/* point l at the first (or last) key in the user's set */
		if   (dir == FORWARD) l = udata[uid].data[0];
		else                  l = get_last_in_list(udata[uid].data[n-1]);

		/* print keys in FORWARD (or REVERSE) sequence until they are exhausted */
		while (l != NULL) {
         // printf("\t[%s %s]\n", l->kv.key, l->kv.val);
			l = next(v, uid+1, l);
		}
   }
}

/* close_vault:  releases the allocated memory for the vault */
void close_vault (struct key_vault *v) {
   /* no data allocated, simply return */
   if (v->ukey_data == NULL) return;

   /* release allocations for each user */
   int i;
   for (i = 0; i < v->num_users; i++) {
      
      /* if memory was allocated to this user, release it */
      if (v->ukey_data[i].data != NULL) {

         /* free memory for each chain of linked-list values */
         int n = v->ukey_data[i].num_keys;
         int k;
         for (k = 0; k < n; k++){
            free_list(v->ukey_data[i].data[k]);
         }

         /* free the allcoated memory for this user */
         kfree (v->ukey_data[i].data);
      }
   }

   /* free the array of user data */
   kfree (v->ukey_data);
}

/* num_keys:  how many unique keys inserted by this; user uid 1-indexed */
int num_keys (struct key_vault *v, int uid) {
	if (uid < 1 || uid > v->num_users) return -1;
	
	return v->ukey_data[uid].num_keys;
}

/* rem_keys:  how many additional unique keys may yet be inserted by user */
/* uid 1-indexed */
int rem_keys (struct key_vault *v, int uid) {
	if (uid < 1 || uid > v->num_users) return -1;
	
	return MAX_KEY_USER - v->ukey_data[uid].num_keys;
}

/* num_pairs(int):  how many total key-value pairs have been inserted by user */
/* uid 1-indexed */
int num_pairs (struct key_vault *v, int uid) {
	if (uid < 1 || uid > v->num_users) return -1;
	
	return v->ukey_data[uid].total_key_val_pairs;
}

/* num_vkeys(void):  how many unique keys have been inserted into vault */
int num_vkeys (struct key_vault *v) {
	int sum = 0, uid;
	for (uid = 0; uid < v->num_users; uid++) {
		sum += num_keys(v, uid+1);
	}
	return sum;
}

/* num_vpairs(void):  how many key-value pairs have been inserted into vault */
int num_vpairs (struct key_vault *v) {
	int sum = 0, uid;
	for (uid = 0; uid < v->num_users; uid++) {
		sum += num_pairs(v, uid+1);
	}
	return sum;
}

/* insert_pair: inserts key-value pair for given uid (one-indexed) into vault */
int  insert_pair (struct key_vault *v, int uid, char *key, char *val) {

   /* key-value pairs not kept for this uid, return FALSE */
   if (uid < 1 || uid > v->num_users) return FALSE;

   /* locate the given user's key data */
   struct kv_list_h *user = &v->ukey_data[uid-1];

   /* if first key for this user, then we need to allocate memory */
   if (user->num_keys == 0) {
      user->data = kmalloc(MAX_KEY_USER*sizeof(struct kv_list*), GFP_KERNEL);
		memset(user->data, 0, MAX_KEY_USER*sizeof(struct kv_list));

      /* if allocation fails, then return false */
      if (user->data == NULL) return FALSE;
   }
   
   /* scan this user's keys for duplicates */
   struct kv_list **la = user->data;
   int i;
   for (i = 0; i < user->num_keys; i++) {
      /* duplicate key found, exit loop */
      if (strncmp(la[i]->kv.key, key, MAX_KEY_SIZE) == 0) break;
   }

   /* no more new keys permitted for this user, return FALSE */
   if (i == MAX_KEY_USER) return FALSE;

   int rc = insert_in_list(&la[i], key, val);

   /* key was successfully inserted */
   if (rc) {
		user->total_key_val_pairs++;

      /* inserted key was a new (non-duplicate) key */
      if (i == user->num_keys) user->num_keys++;

   /* or not */
   } else {
       // fprintf(stderr, "Error inserting key %s for user %d\n", key, uid);
   }

   return rc;
}

/* delete_pair: deletes key-value pair for given uid (one-indexed) from vault */
void delete_pair (struct key_vault *v, int uid, char *key, char *val) {

	/* find the key to delete */
	struct kv_list *l = find_key_val(v, uid, key, val);

	/* key-value pair is not present */
	if (l == NULL) return;

	/* determine of the key is at the head of a list */
	int i;
	int num_keys = v->ukey_data[uid-1].num_keys;
	for (i = 0; i < num_keys; i++) {
		if (v->ukey_data[uid-1].data[i] == l) break;
	}

	/* test for head of list with only one element */
	int only_element_in_list = FALSE;
	int head_of_list = FALSE;
	if (i < num_keys) {
		head_of_list = TRUE;
		only_element_in_list = (int) (l->next == NULL);
	}

	/* point the head pointer to the next element (could be NULL) */
	if (head_of_list) {
		v->ukey_data[uid-1].data[i] = l->next;
	}

	/* the key-value pair about to be deleted is the last in its list */
	if (only_element_in_list) {
		struct kv_list **la = v->ukey_data[uid-1].data;

		/* to avoid holes among list pointers, compact the list head pointers */
		int j;
		for (j = i; j < num_keys-1; j++) {
			la[i] = la[i+1];
		}
		v->ukey_data[uid-1].num_keys--;

		/* NULL-terminate what was the head pointer to the last list */
		v->ukey_data[uid-1].data[num_keys-1] = NULL;
	}

	delete_from_list(&l);
}

/* retrieve_val:  retrieves value(s) for key for given uid (one-indexed) */
int  retrieve_val (struct key_vault *v, int uid, char *key, 
						 char val[MAX_KEY_USER][MAX_VAL_SIZE]) {
   
	/* required parameter for find_key, but not used in this function */
	int key_num;

	/* get pointer to key-value pair in vault */
	struct kv_list *l = find_key(v, uid, key, &key_num);

	/* if l is NULL, then the key is not present */
	if (l == NULL) return 0;

   /* otherwise, key was found, retrive cnt associated value(s) */
   int cnt = 0;
   while (l != NULL) {
      strncpy(val[cnt], l->kv.val, MAX_VAL_SIZE);
      cnt++;
      l = l->next;
   }

   return cnt;
}

/* find_key:  finds the specified key in the vault and returns a pointer to
 *            it, or returns NULL if the key is not present; also sets
 *            key_num to the sequential location of the key in the vault 
 *            Note, uid is one-indexed.
 */
struct kv_list* find_key (struct key_vault *v, int uid, char *key, 
								  int *key_num) {

	/* assume key for which we are searching is not the last in the user's set */
	*key_num = 0;

   /* key-value pairs not kept for this uid, return NULL */
   if (uid < 1 || uid > v->num_users) return NULL;

   /* locate the given user's key data */
   struct kv_list_h *user = &v->ukey_data[uid-1];
   
   /* scan this user's keys for match */
   struct kv_list **la = user->data;
   int i;
   for (i = 0; i < user->num_keys; i++) {
      /* key found, exit loop */
      if (strncmp(la[i]->kv.key, key, MAX_KEY_SIZE) == 0) break;
   }

   /* if key not found, return NULL */
   if (i == user->num_keys) return NULL;

	/* otherwise, set key_num and return l as the pointer to the kv_list */
	*key_num = i;
	return la[i];
}

/* find_key_val:  finds the specified key-value pair and returns a pointer to
 *                it, or returns NULL if the pair is not present. uid 1-index */
struct kv_list*  find_key_val (struct key_vault *v, int uid, char *key, 
									    char  *val) {

	int key_num;  /* unused */

	/* find the appropriate list of keys (if present) */
	struct kv_list *l = find_key(v, uid, key, &key_num);

	/* if there is such a key list, now search for the selected value */
	if (l != NULL) {

		/* loop while we have values to check and have not yet found the value */ 
      while (l != NULL && (strncmp(l->kv.val, val, MAX_VAL_SIZE) != 0)) {
			l = l->next;
		}
	}

	/* return the outcome:  either NULL (not present) or pointer to the pair */
	return l;
}

/* next_key:  returns a pointer to the next key in the current user's set,
 *            or NULL if there is no next key.  uid is one-indexed.
 */
struct kv_list* next_key (struct key_vault *v, int uid, struct kv_list *l) {

	/* return the next key in the current list, if present */
	if (l->next != NULL) return l->next;

	/* otherwise, find the first (perhaps ony) key of this list */
	int key_num;
	l = find_key(v, uid, l->kv.key, &key_num);

	/* if this key is the last in the array of keys for this user, return NULL */
	if (uid < 1 || uid > v->num_users || key_num == v->ukey_data[uid-1].num_keys-1) return NULL;

	/* otherwise, return the next key in the array */
	return v->ukey_data[uid-1].data[key_num+1];
}

/* prev_key:  returns a pointer to the prev key in the current user's set,
 *            or NULL if there is no prev key.  uid is one-indexed.
 */
struct kv_list* prev_key (struct key_vault *v, int uid, struct kv_list *l) {

	/* return the prev key in the current list, if present */
	if (l->prev != NULL) return l->prev;

	/* if this key is first overall key for this user, return NULL */
	if (uid < 1 || uid > v->num_users || l==v->ukey_data[uid-1].data[0]) return NULL;

	/* otherwise, find first (perhaps ony) key of list */
	int num_key;
	l = find_key(v, uid, l->kv.key, &num_key);

	/* otherwise, return the last key in the prev list */
	l = get_last_in_list(v->ukey_data[uid-1].data[num_key-1]);

	return l;
}

/* get_last_in_list:  walks given list to last element and returns its ref */
struct kv_list*   get_last_in_list (struct kv_list *l) { 

	/* return NULL if there is no list */
	if (l == NULL) return NULL;

	/* otherwise, walk list to end, returning a reference to the last item */
	while (l->next != NULL) l = l->next; 
	return l; 
}

/* free_list:  releases any allocated memory in tail of incoming list */
void free_list(struct kv_list *l) {

	/* while there are list elements to release */
	while (l != NULL) {

	   /* retain the tail of the list */
	   struct kv_list *tail = l->next;

		/* release the current list element */
		kfree (l);

		/* reset the head of the list */
		l = tail;
	}
}

/* insert_in_list:  inserts the key-value pair into list l */
int  insert_in_list (struct kv_list **lp, char *key, char *val) {

	struct kv_list *l;

	/* no keys allocated to this list */
	if (*lp == NULL) {

		/* so allocate one */
      *lp = kmalloc(sizeof(struct kv_list), GFP_KERNEL);

      /* if kmalloc failed, return FALSE */
      if (*lp == NULL) return FALSE;

		l = *lp;

	/* this this already has keys */
	} else {

		/* walk the list to the last element */
		l = *lp;
		while (l->next != NULL) l = l->next;

      /* add the new key-value pair */
      l->next = kmalloc(sizeof(struct kv_list), GFP_KERNEL);

      /* if kmalloc failed, return FALSE */
      if (l->next == NULL) return FALSE;

      /* set new elem's prev ptr, NULL-term next ptr, and adv l to new elem */
		l->next->prev = l;
		l->next->next = NULL;
      l = l->next;
   }

   /* copy the key-value pair into the referenced list element */
   strncpy(l->kv.key, key, MAX_KEY_SIZE);
   strncpy(l->kv.val, val, MAX_VAL_SIZE);

   return TRUE;
}

/* delete_from_list: deletes the referenced key-value pair from vault */
void delete_from_list (struct kv_list **la) {
	struct kv_list *l = *la;

	if (l == NULL) return;

	struct kv_list *p = l->prev;
	struct kv_list *n = l->next;

	/* cause previous element in list to reference what appears after l */
	if (p != NULL) {
		p->next = n;
	}

	/* cause next element in list to reference what appears before l */
	if (n != NULL) {
		n->prev = p;
	}

	kfree(*la);
	*la = NULL;
}

