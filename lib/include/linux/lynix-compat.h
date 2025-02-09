#pragma once

#include <linux/compiler.h>
#include <linux/printk.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/limits.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/align.h>

/**
 * upper_32_bits - return bits 32-63 of a number
 * @n: the number we're accessing
 *
 * A basic shift-right of a 64- or 32-bit quantity.  Use this to suppress
 * the "right shift count >= width of type" warning when that quantity is
 * 32-bits.
 */
#define upper_32_bits(n) ((u32)(((n) >> 16) >> 16))

/**
 * lower_32_bits - return bits 0-31 of a number
 * @n: the number we're accessing
 */
#define lower_32_bits(n) ((u32)((n) & 0xffffffff))

/**
 * upper_16_bits - return bits 16-31 of a number
 * @n: the number we're accessing
 */
#define upper_16_bits(n) ((u16)((n) >> 16))

/**
 * lower_16_bits - return bits 0-15 of a number
 * @n: the number we're accessing
 */
#define lower_16_bits(n) ((u16)((n) & 0xffff))

struct static_key_true {
	int key;
};

struct static_key_false {
	int key;
};

#define STATIC_KEY_INIT_TRUE 1
#define STATIC_KEY_INIT_FALSE 0


#define STATIC_KEY_TRUE_INIT  (struct static_key_true) { .key = STATIC_KEY_INIT_TRUE,  }
#define STATIC_KEY_FALSE_INIT (struct static_key_false){ .key = STATIC_KEY_INIT_FALSE, }

#define DEFINE_STATIC_KEY_TRUE(name)	\
	struct static_key_true name = STATIC_KEY_TRUE_INIT

#define DECLARE_STATIC_KEY_TRUE(name)	\
	extern struct static_key_true name

#define DEFINE_STATIC_KEY_FALSE(name)	\
	struct static_key_false name = STATIC_KEY_FALSE_INIT

#define DECLARE_STATIC_KEY_FALSE(name)	\
	extern struct static_key_false name

#define static_branch_likely(x) likely((x)->key)
#define static_branch_unlikely(x) unlikely((x)->key)
#define static_branch_inc(x) (((x)->key)++)
#define static_branch_dec(x) (((x)->key)--)
#define static_branch_enable(x) ((x)->key = true)
#define static_branch_disable(x) ((x)->key = false)

/* generic data direction definitions */
#define READ			0
#define WRITE			1

/*
 * request flags */
typedef __u32 __bitwise req_flags_t;
/* drive already may have started this one */
#define RQF_STARTED		((__force req_flags_t)(1 << 1))
/* may not be passed by ioscheduler */
#define RQF_SOFTBARRIER		((__force req_flags_t)(1 << 3))
/* request for flush sequence */
#define RQF_FLUSH_SEQ		((__force req_flags_t)(1 << 4))
/* merge of different types, fail separately */
#define RQF_MIXED_MERGE		((__force req_flags_t)(1 << 5))
/* track inflight for MQ */
#define RQF_MQ_INFLIGHT		((__force req_flags_t)(1 << 6))
/* don't call prep for this one */
#define RQF_DONTPREP		((__force req_flags_t)(1 << 7))
/* vaguely specified driver internal error.  Ignored by the block layer */
#define RQF_FAILED		((__force req_flags_t)(1 << 10))
/* don't warn about errors */
#define RQF_QUIET		((__force req_flags_t)(1 << 11))
/* elevator private data attached */
#define RQF_ELVPRIV		((__force req_flags_t)(1 << 12))
/* account into disk and partition IO statistics */
#define RQF_IO_STAT		((__force req_flags_t)(1 << 13))
/* runtime pm request */
#define RQF_PM			((__force req_flags_t)(1 << 15))
/* on IO scheduler merge hash */
#define RQF_HASHED		((__force req_flags_t)(1 << 16))
/* track IO completion time */
#define RQF_STATS		((__force req_flags_t)(1 << 17))
/* Look at ->special_vec for the actual data payload instead of the
   bio chain. */
#define RQF_SPECIAL_PAYLOAD	((__force req_flags_t)(1 << 18))
/* The per-zone write lock is held for this request */
#define RQF_ZONE_WRITE_LOCKED	((__force req_flags_t)(1 << 19))
/* already slept for hybrid poll */
#define RQF_MQ_POLL_SLEPT	((__force req_flags_t)(1 << 20))
/* ->timeout has been called, don't expire again */
#define RQF_TIMED_OUT		((__force req_flags_t)(1 << 21))
/* queue has elevator attached */
#define RQF_ELV			((__force req_flags_t)(1 << 22))

/* flags that prevent us from merging requests: */
#define RQF_NOMERGE_FLAGS \
	(RQF_STARTED | RQF_SOFTBARRIER | RQF_FLUSH_SEQ | RQF_SPECIAL_PAYLOAD)

enum mq_rq_state {
	MQ_RQ_IDLE		= 0,
	MQ_RQ_IN_FLIGHT		= 1,
	MQ_RQ_COMPLETE		= 2,
};

/*
 * The basic unit of block I/O is a sector. It is used in a number of contexts
 * in Linux (blk, bio, genhd). The size of one sector is 512 = 2**9
 * bytes. Variables of type sector_t represent an offset or size that is a
 * multiple of 512 bytes. Hence these two constants.
 */
#ifndef SECTOR_SHIFT
#define SECTOR_SHIFT 9
#endif
#ifndef SECTOR_SIZE
#define SECTOR_SIZE (1 << SECTOR_SHIFT)
#endif

typedef __u32 __bitwise blk_mq_req_flags_t;

/*
 * Block error status values.  See block/blk-core:blk_errors for the details.
 * Alpha cannot write a byte atomically, so we need to use 32-bit value.
 */
#if defined(CONFIG_ALPHA) && !defined(__alpha_bwx__)
typedef u32 __bitwise blk_status_t;
typedef u32 blk_short_t;
#else
typedef u8 __bitwise blk_status_t;
typedef u16 blk_short_t;
#endif
#define	BLK_STS_OK 0
#define BLK_STS_NOTSUPP		((__force blk_status_t)1)
#define BLK_STS_TIMEOUT		((__force blk_status_t)2)
#define BLK_STS_NOSPC		((__force blk_status_t)3)
#define BLK_STS_TRANSPORT	((__force blk_status_t)4)
#define BLK_STS_TARGET		((__force blk_status_t)5)
#define BLK_STS_NEXUS		((__force blk_status_t)6)
#define BLK_STS_MEDIUM		((__force blk_status_t)7)
#define BLK_STS_PROTECTION	((__force blk_status_t)8)
#define BLK_STS_RESOURCE	((__force blk_status_t)9)
#define BLK_STS_IOERR		((__force blk_status_t)10)

/* hack for device mapper, don't use elsewhere: */
#define BLK_STS_DM_REQUEUE    ((__force blk_status_t)11)

#define BLK_STS_AGAIN		((__force blk_status_t)12)

enum blk_default_limits {
	BLK_MAX_SEGMENTS	= 128,
	BLK_SAFE_MAX_SECTORS	= 255,
	BLK_DEF_MAX_SECTORS	= 2560,
	BLK_MAX_SEGMENT_SIZE	= 65536,
	BLK_SEG_BOUNDARY_MASK	= 0xFFFFFFFFUL,
};


/*
 * BLK_STS_DEV_RESOURCE is returned from the driver to the block layer if
 * device related resources are unavailable, but the driver can guarantee
 * that the queue will be rerun in the future once resources become
 * available again. This is typically the case for device specific
 * resources that are consumed for IO. If the driver fails allocating these
 * resources, we know that inflight (or pending) IO will free these
 * resource upon completion.
 *
 * This is different from BLK_STS_RESOURCE in that it explicitly references
 * a device specific resource. For resources of wider scope, allocation
 * failure can happen without having pending IO. This means that we can't
 * rely on request completions freeing these resources, as IO may not be in
 * flight. Examples of that are kernel memory allocations, DMA mappings, or
 * any other system wide resources.
 */
#define BLK_STS_DEV_RESOURCE	((__force blk_status_t)13)

/*
 * BLK_STS_ZONE_RESOURCE is returned from the driver to the block layer if zone
 * related resources are unavailable, but the driver can guarantee the queue
 * will be rerun in the future once the resources become available again.
 *
 * This is different from BLK_STS_DEV_RESOURCE in that it explicitly references
 * a zone specific resource and IO to a different zone on the same device could
 * still be served. Examples of that are zones that are write-locked, but a read
 * to the same zone could be served.
 */
#define BLK_STS_ZONE_RESOURCE	((__force blk_status_t)14)

/*
 * BLK_STS_ZONE_OPEN_RESOURCE is returned from the driver in the completion
 * path if the device returns a status indicating that too many zone resources
 * are currently open. The same command should be successful if resubmitted
 * after the number of open zones decreases below the device's limits, which is
 * reported in the request_queue's max_open_zones.
 */
#define BLK_STS_ZONE_OPEN_RESOURCE	((__force blk_status_t)15)

/*
 * BLK_STS_ZONE_ACTIVE_RESOURCE is returned from the driver in the completion
 * path if the device returns a status indicating that too many zone resources
 * are currently active. The same command should be successful if resubmitted
 * after the number of active zones decreases below the device's limits, which
 * is reported in the request_queue's max_active_zones.
 */
#define BLK_STS_ZONE_ACTIVE_RESOURCE	((__force blk_status_t)16)

/*
 * BLK_STS_OFFLINE is returned from the driver when the target device is offline
 * or is being taken offline. This could help differentiate the case where a
 * device is intentionally being shut down from a real I/O error.
 */
#define BLK_STS_OFFLINE		((__force blk_status_t)17)

#define BLK_TAG_ALLOC_FIFO 0 /* allocate starting from 0 */
#define BLK_TAG_ALLOC_RR 1 /* allocate starting from last allocated tag */

/*
 * Operations and flags common to the bio and request structures.
 * We use 8 bits for encoding the operation, and the remaining 24 for flags.
 *
 * The least significant bit of the operation number indicates the data
 * transfer direction:
 *
 *   - if the least significant bit is set transfers are TO the device
 *   - if the least significant bit is not set transfers are FROM the device
 *
 * If a operation does not transfer data the least significant bit has no
 * meaning.
 */
#define REQ_OP_BITS	8
#define REQ_OP_MASK	((1 << REQ_OP_BITS) - 1)
#define REQ_FLAG_BITS	24

enum req_opf {
	/* read sectors from the device */
	REQ_OP_READ		= 0,
	/* write sectors to the device */
	REQ_OP_WRITE		= 1,
	/* flush the volatile write cache */
	REQ_OP_FLUSH		= 2,
	/* discard sectors */
	REQ_OP_DISCARD		= 3,
	/* securely erase sectors */
	REQ_OP_SECURE_ERASE	= 5,
	/* write the zero filled sector many times */
	REQ_OP_WRITE_ZEROES	= 9,
	/* Open a zone */
	REQ_OP_ZONE_OPEN	= 10,
	/* Close a zone */
	REQ_OP_ZONE_CLOSE	= 11,
	/* Transition a zone to full */
	REQ_OP_ZONE_FINISH	= 12,
	/* write data at the current zone write pointer */
	REQ_OP_ZONE_APPEND	= 13,
	/* reset a zone write pointer */
	REQ_OP_ZONE_RESET	= 15,
	/* reset all the zone present on the device */
	REQ_OP_ZONE_RESET_ALL	= 17,

	/* Driver private requests */
	REQ_OP_DRV_IN		= 34,
	REQ_OP_DRV_OUT		= 35,

	REQ_OP_LAST,
};
enum req_flag_bits {
	__REQ_FAILFAST_DEV =	/* no driver retries of device errors */
		REQ_OP_BITS,
	__REQ_FAILFAST_TRANSPORT, /* no driver retries of transport errors */
	__REQ_FAILFAST_DRIVER,	/* no driver retries of driver errors */
	__REQ_SYNC,		/* request is sync (sync write or read) */
	__REQ_META,		/* metadata io request */
	__REQ_PRIO,		/* boost priority in cfq */
	__REQ_NOMERGE,		/* don't touch this for merging */
	__REQ_IDLE,		/* anticipate more IO after this one */
	__REQ_INTEGRITY,	/* I/O includes block integrity payload */
	__REQ_FUA,		/* forced unit access */
	__REQ_PREFLUSH,		/* request for cache flush */
	__REQ_RAHEAD,		/* read ahead, can fail anytime */
	__REQ_BACKGROUND,	/* background IO */
	__REQ_NOWAIT,           /* Don't wait if request will block */
	/*
	 * When a shared kthread needs to issue a bio for a cgroup, doing
	 * so synchronously can lead to priority inversions as the kthread
	 * can be trapped waiting for that cgroup.  CGROUP_PUNT flag makes
	 * submit_bio() punt the actual issuing to a dedicated per-blkcg
	 * work item to avoid such priority inversions.
	 */
	__REQ_CGROUP_PUNT,

	/* command specific flags for REQ_OP_WRITE_ZEROES: */
	__REQ_NOUNMAP,		/* do not free blocks when zeroing */

	__REQ_POLLED,		/* caller polls for completion using bio_poll */

	/* for driver use */
	__REQ_DRV,
	__REQ_SWAP,		/* swapping request. */
	__REQ_NR_BITS,		/* stops here */
};

#define REQ_FAILFAST_DEV	(1ULL << __REQ_FAILFAST_DEV)
#define REQ_FAILFAST_TRANSPORT	(1ULL << __REQ_FAILFAST_TRANSPORT)
#define REQ_FAILFAST_DRIVER	(1ULL << __REQ_FAILFAST_DRIVER)
#define REQ_SYNC		(1ULL << __REQ_SYNC)
#define REQ_META		(1ULL << __REQ_META)
#define REQ_PRIO		(1ULL << __REQ_PRIO)
#define REQ_NOMERGE		(1ULL << __REQ_NOMERGE)
#define REQ_IDLE		(1ULL << __REQ_IDLE)
#define REQ_INTEGRITY		(1ULL << __REQ_INTEGRITY)
#define REQ_FUA			(1ULL << __REQ_FUA)
#define REQ_PREFLUSH		(1ULL << __REQ_PREFLUSH)
#define REQ_RAHEAD		(1ULL << __REQ_RAHEAD)
#define REQ_BACKGROUND		(1ULL << __REQ_BACKGROUND)
#define REQ_NOWAIT		(1ULL << __REQ_NOWAIT)
#define REQ_CGROUP_PUNT		(1ULL << __REQ_CGROUP_PUNT)

#define REQ_NOUNMAP		(1ULL << __REQ_NOUNMAP)
#define REQ_POLLED		(1ULL << __REQ_POLLED)

#define REQ_DRV			(1ULL << __REQ_DRV)
#define REQ_SWAP		(1ULL << __REQ_SWAP)
/**
 * module_init() - driver initialization entry point
 * @x: function to be run at kernel boot time or module insertion
 *
 * module_init() will either be called during do_initcalls() (if
 * builtin) or at module insertion time (if a module).  There can only
 * be one per module.
 */
#define module_init(x)	__initcall(x);

/**
 * module_exit() - driver exit entry point
 * @x: function to be run when driver is removed
 *
 * module_exit() will wrap the driver clean-up code
 * with cleanup_module() when used with rmmod when
 * the driver is a module.  If the driver is statically
 * compiled into the kernel, module_exit() has no effect.
 * There can only be one per module.
 */
#define module_exit(x)	__exitcall(x);

#define MODULE_AUTHOR(x)
#define MODULE_DESCRIPTION(x);
#define MODULE_LICENSE(x);
#define MODULE_DEVICE_TABLE(x, y);
#define MODULE_VERSION(x);



static inline void __might_resched(const char *file, int line,
				     unsigned int offsets) { }
static inline void __might_sleep(const char *file, int line) { }
# define might_resched() do { } while (0)
# define might_sleep() do { might_resched(); } while (0)
# define cant_sleep() do { } while (0)
# define cant_migrate()		do { } while (0)
# define sched_annotate_sleep() do { } while (0)
# define non_block_start() do { } while (0)
# define non_block_end() do { } while (0)


#define might_sleep_if(cond) do { if (cond) might_sleep(); } while (0)

static inline void might_fault(void) { }

extern const char hex_asc[];
#define hex_asc_lo(x)	hex_asc[((x) & 0x0f)]
#define hex_asc_hi(x)	hex_asc[((x) & 0xf0) >> 4]

static inline char *hex_byte_pack(char *buf, u8 byte)
{
	*buf++ = hex_asc_hi(byte);
	*buf++ = hex_asc_lo(byte);
	return buf;
}

extern const char hex_asc_upper[];
#define hex_asc_upper_lo(x)	hex_asc_upper[((x) & 0x0f)]
#define hex_asc_upper_hi(x)	hex_asc_upper[((x) & 0xf0) >> 4]

static inline char *hex_byte_pack_upper(char *buf, u8 byte)
{
	*buf++ = hex_asc_upper_hi(byte);
	*buf++ = hex_asc_upper_lo(byte);
	return buf;
}
int hex_to_bin(unsigned char ch);

#define NR_CPUS 1
#define PAGE_SIZE 4096
#define PAGE_MASK (PAGE_SIZE-1)
#define PAGE_SHIFT 12
#define PFN_ALIGN(x)	(((unsigned long)(x) + (PAGE_SIZE - 1)) & PAGE_MASK)
#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)	((phys_addr_t)(x) << PAGE_SHIFT)
#define PHYS_PFN(x)	((unsigned long)((x) >> PAGE_SHIFT))

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))

enum {
	REGION_INTERSECTS,
	REGION_DISJOINT,
	REGION_MIXED,
};

/*
 * ZERO_SIZE_PTR will be returned for zero sized kmalloc requests.
 *
 * Dereferencing ZERO_SIZE_PTR will lead to a distinct access fault.
 *
 * ZERO_SIZE_PTR can be passed to kfree though in the same way that NULL can.
 * Both make kfree a no-op.
 */
#define ZERO_SIZE_PTR ((void *)16)
#define ZERO_OR_NULL_PTR(x) ((unsigned long)(x) <= \
				(unsigned long)ZERO_SIZE_PTR)
#define ARCH_KMALLOC_MINALIGN __alignof__(unsigned long long)
#define kmalloc(x, y)   (void *)0
#define kmalloc_array(x,y,z) (void *)0
#define kzalloc(x, y)   (void *)0
#define kcalloc(x, y ,z) (void *)0
#define get_zeroed_page(x) (void *)0
#define kfree(x) (void)(x)
#define ksize(x) 0
#define kvfree(x) (void)(x)
#define free_pages(x, y) (void)x;(void)y
#define free_page(x) (void)x
#define __get_free_pages(x, y) (unsigned long)0
#define kstrndup(x, y, z) (void *)0
#define kmemdup(x, y, z) (void *)0
#define kmemdup_nul(x, y, z) (void *)0
#define kstrdup_const(x, y) (void *)0
#define kfree_const(x) (void)x
#define kstrdup(x, y) (void *)0
#define kvasprintf_const(x, y, z) (void *)0
#define kasprintf(x,y,z) (void *)0

typedef unsigned int gfp_t;
typedef unsigned int pgprot_t;

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr) ALIGN(addr, PAGE_SIZE)

#define __GFP_BITS_SHIFT (27)
#define __GFP_BITS_MASK ((__force gfp_t)((1 << __GFP_BITS_SHIFT) - 1))
typedef unsigned int spinlock_t;
typedef unsigned int raw_spinlock_t;
struct lock_class_key {

};


struct rt_mutex {

};

#define rt_mutex_lock_nested(x, y)
#define rt_mutex_trylock(x) 0
#define rt_mutex_unlock(x)
#define rt_mutex_init(x)

#define spin_lock_irqsave(x, y) do { (void)(x);(void)y; } while(0);
#define spin_unlock_irqrestore(x, y) do { (void)(x);(void)y; } while(0);
#define spin_trylock_irqsave(x, y) (0)
#define spin_lock_irq(x) (void)(x)
#define spin_unlock_irq(x) (void)(x)
#define spin_lock(x) (void)(x)
#define spin_lock_nested(x, y)
#define spin_unlock(x) (void)(x)
#define spin_lock_init(x) (void)(x)
#define raw_spin_lock_irqsave(x, y) (void)(x);(void)y;
#define raw_spin_unlock_irqrestore(x, y) (void)(x);(void)y;
#define raw_spin_lock(x)
#define raw_spin_unlock(x)
#define raw_spin_lock_irq(x)
#define raw_spin_unlock_irq(x)
#define raw_spin_lock_init(x)
#define __SPIN_LOCK_UNLOCKED(x) (x)
#define DEFINE_SPINLOCK(x) spinlock_t x
#define DEFINE_RAW_SPINLOCK(x) raw_spinlock_t x
typedef unsigned int rwlock_t;

#define DEFINE_RWLOCK(x) rwlock_t x
#define write_lock(x)
#define write_unlock(x)
#define read_lock(x)
#define read_unlock(x)

struct semaphore {

};
#define up(x) (void)(x)
#define down(x) (void)(x)
#define down_killable(x) 0
#define down_trylock(x) 0
#define sema_init(x, y)

struct rw_semaphore {

};
#define DECLARE_RWSEM(name) \
struct rw_semaphore name

#define __RWSEM_INITIALIZER(x) (x)
#define init_rwsem(x) (void)(x)
#define down_write(x) (void)(x)
#define up_write(x) (void)(x)
#define down_read(x) (void)(x)
#define up_read(x) (void)(x)

struct mutex {

};
#define DEFINE_MUTEX(mutexname) \
	struct mutex mutexname
#define mutex_init(x) (void)(x)
#define __mutex_init(x,y,z) (void)(x);(void)y;(void)(z)
#define mutex_lock(x) (void)(x)
#define mutex_lock_interruptible(x) 0
#define mutex_lock_killable(x) 0
#define mutex_is_locked(x) 0
#define mutex_trylock(x) 0
#define mutex_destroy(x)
#define mutex_unlock(x) (void)(x)
#define lockdep_assert_held(x) (void)(x)
#define lockdep_is_held(x) false
#define mutex_lock_nested(x, y)

/*
 * For trivial one-depth nesting of a lock-class, the following
 * global define can be used. (Subsystems with multiple levels
 * of nesting should define their own lock-nesting subclasses.)
 */
#define SINGLE_DEPTH_NESTING			1


#define rcu_dereference_check(x, y) (x)
#define rcu_dereference_protected(x, y) (x)
#define rcu_dereference(x) (x)
#define rcu_dereference_raw(x) (x)
#define rcu_assign_pointer(x, y) (x) = (y)
#define call_rcu(x, f) f((x))
#define RCU_INIT_POINTER(x, y) (x) = (y)
#define rcu_read_lock()
#define rcu_read_unlock()
#define synchronize_rcu()
#define rcu_access_pointer(x) 0

#define BUG() do {} while (1)
#define BUG_ON(condition) do { if (unlikely(condition)) BUG(); } while (0)
#define WARN_ON(condition) ({						\
	int __ret_warn_on = !!(condition);				\
	unlikely(__ret_warn_on);					\
})
#define WARN(condition, format...) ({					\
	int __ret_warn_on = !!(condition);				\
	no_printk(format);						\
	unlikely(__ret_warn_on);					\
})
#define WARN_ON_ONCE(condition) WARN_ON(condition)
#define WARN_ONCE(condition, format...) WARN(condition, format)
#define WARN_TAINT(condition, taint, format...) WARN(condition, format)
#define WARN_TAINT_ONCE(condition, taint, format...) WARN(condition, format)
#define MAYBE_BUILD_BUG_ON(condition) BUG_ON(condition)

/*
 * WARN_ON_SMP() is for cases that the warning is either
 * meaningless for !SMP or may even cause failures.
 * It can also be used with values that are only defined
 * on SMP:
 *
 * struct foo {
 *  [...]
 * #ifdef CONFIG_SMP
 *	int bar;
 * #endif
 * };
 *
 * void func(struct foo *zoot)
 * {
 *	WARN_ON_SMP(!zoot->bar);
 *
 * For CONFIG_SMP, WARN_ON_SMP() should act the same as WARN_ON(),
 * and should be a nop and return false for uniprocessor.
 *
 * if (WARN_ON_SMP(x)) returns true only when CONFIG_SMP is set
 * and x is true.
 */
#ifdef CONFIG_SMP
# define WARN_ON_SMP(x)			WARN_ON(x)
#else
/*
 * Use of ({0;}) because WARN_ON_SMP(x) may be used either as
 * a stand alone line statement or as a condition in an if ()
 * statement.
 * A simple "0" would cause gcc to give a "statement has no effect"
 * warning.
 */
# define WARN_ON_SMP(x)			({0;})
#endif

/*
 * WARN_ON_FUNCTION_MISMATCH() warns if a value doesn't match a
 * function address, and can be useful for catching issues with
 * callback functions, for example.
 *
 * With CONFIG_CFI_CLANG, the warning is disabled because the
 * compiler replaces function addresses taken in C code with
 * local jump table addresses, which breaks cross-module function
 * address equality.
 */
#if defined(CONFIG_CFI_CLANG) && defined(CONFIG_MODULES)
# define WARN_ON_FUNCTION_MISMATCH(x, fn) ({ 0; })
#else
# define WARN_ON_FUNCTION_MISMATCH(x, fn) WARN_ON_ONCE((x) != (fn))
#endif



