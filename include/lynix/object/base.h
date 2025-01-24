#pragma once

#include <linux/xarray.h>
#include <linux/notifier.h>
#include <linux/klist.h>
#include <linux/maple_tree.h>

struct keyres_node;

/**
 * struct key_vtable: refer to nested combinations structure.
 *
 */
struct key_vtable {
    /*! Dispatches an request to a engine. */
    void (*dispatch)(struct keyres_node * const me, void const * const q);     /* nest or combine */
};

/**
 * struct keyres_node: <k_index, k_size> <<= <0, ULONG_MAX>
 */
struct keyres_node {
    struct list_head k_entry;
    size_t k_index;
    size_t k_size;
    struct key_vtable const *k_vptr;
};

/**
 * struct kobject_private: refer to nested combinations structure.
 *
 */
struct kobject_private {
    struct klist_node knode_klink;
    union {
        struct maple_tree kobject_ma_head; /* virtual */
        struct xarray kobject_xa_head; /* physics : mark as alloc/free status .etc */
    };

	spinlock_t keyres_lock;
	struct list_head keyres_head;
};

/**
 * struct klink_private: refer to relationship structure.
 *
 */
struct klink_private {
    struct xarray klink_head;
	struct klist klist_kobjects;

    struct klist klist_childrens;
    struct klist_node knode_children;
    struct klink_private *klink_parent;

    union {
        struct atomic_notifier_head klink_notifier_chain;
        struct sm_notifier_head klink_notifier_state;
    };
};

/**
 * struct kobject
 */
struct kobject {
    struct kobject_private *p;
};

/**
 * struct klink
 *
 */
struct klink {
    struct klink_private *p;
};





