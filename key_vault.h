
/* Author:  Keith Shomper
 * Date:    8 Nov 2016
 * Purpose: Supports HW4 for CS3320
 * Version: 3 */

#define MAX_KEY_SIZE 20
#define MAX_VAL_SIZE 20
#define MAX_KEY_USER 20

#define FORWARD       0
#define REVERSE       1

#define FALSE         0
#define TRUE          1

/* structure to hold the key-value pairs                           */
struct key_val {
	char key[MAX_KEY_SIZE];
	char val[MAX_VAL_SIZE];
};

/* allows the key-value pairs to be grouped into a linked list     */
struct kv_list {
	struct key_val  kv;
	struct kv_list *next;
	struct kv_list *prev;
};

/* hold information about a list, including a pointer to the head */
struct kv_list_h {
	int              total_key_val_pairs;
	int              num_keys;
	struct kv_list **data;
	struct kv_list  *fp;
};

/* the key_vault is essentially an array of kv_list head pointers */
struct key_vault {
	int               num_users;
	struct kv_list_h *ukey_data;
};

/* we cannot use a global handle for the key vault in this code, because this
 * code may be simultaneously operating on vaults of different devices, thus it
 * is necessary that the function call designates WHICH vault is the target of 
 * the given operation. */
// extern struct key_vault v;

/* a typedefed function pointer for walking the data structure sequentially   */
typedef struct kv_list*(*seq_func_ptr)(struct key_vault*, int, struct kv_list*);

/*
 * Function prototypes follow
 */

/* init_vault:  initializes the key vault                                     */
int init_vault (struct key_vault *v, int size);

/* dump_vault:  prints the contents of the vault to stdout for debugging      */
void dump_vault (struct key_vault *v, int dir);

/* close_vault:  releases the allocated memory for the vault                  */
void close_vault (struct key_vault *v);

/* num_keys:  how many unique keys have been inserted by this user            */
int num_keys (struct key_vault *v, int uid);

/* rem_keys:  how many additional unique keys may yet be inserted by user     */
int rem_keys (struct key_vault *v, int uid);

/* num_pairs(int):  how many total key-value pairs have been inserted by user */
int num_pairs (struct key_vault *v, int uid);

/* num_vkeys:  how many unique keys have been inserted into the vault         */
int num_vkeys (struct key_vault *v);

/* num_vpairs(void):  how many key-value pairs have been inserted into vault  */
int num_vpairs (struct key_vault *v);

/* insert_pair: inserts key-value pair for given uid (one-indexed) into vault */
int insert_pair (struct key_vault *v, int uid, char *key, char *val);

/* delete_pair: deletes key-value pair for given uid (one-indexed) from vault */
void delete_pair (struct key_vault *v, int uid, char *key, char *val);

/* retrieve_val:  retrieves val(s) for key for uid (one-indexed) for debugging*/
int retrieve_val (struct key_vault *v, int uid, char *key, 
						char  val[MAX_KEY_USER][MAX_VAL_SIZE]);

/* find_key:  finds the specified key in the vault and returns a pointer to
 *            it, or returns NULL if the key is not present; also sets
 *            key_num to the sequential location of the key in the vault 
 *            Note, uid is one-indexed.                                       */
struct kv_list*  find_key  (struct key_vault *v, int uid, char *key, 
									 int *key_num);

/* find_key_val:  finds the specified key-value pair and returns a pointer to
 *                it, or returns NULL if the pair is not present.             */
struct kv_list*  find_key_val (struct key_vault *v, int uid, char *key, 
									    char  *val);

/* next_key:  returns a pointer to the next key in the current user's set,
 *            or NULL if there is no next key.  uid is one-indexed.           */
struct kv_list*  next_key  (struct key_vault *v, int uid, struct kv_list *l);

/* prev_key:  returns a pointer to the prev key in the current user's set,
 *            or NULL if there is no prev key.  uid is one-indexed.           */
struct kv_list*  prev_key  (struct key_vault *v, int uid, struct kv_list *l);

/* get_last_in_list:  walks given list to last element and returns its ref    */
struct kv_list*  get_last_in_list (struct kv_list *l);

/* free_list:  releases any allocated memory in tail of incoming list         */
void free_list (struct kv_list *l);

/* insert_in_list:  inserts the key-value pair into list l                    */
int insert_in_list (struct kv_list **l, char *key, char *val);

/* delete_from_list: deletes the referenced key-value pair from vault */
void delete_from_list (struct kv_list **l);

