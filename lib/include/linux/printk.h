/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KERNEL_PRINTK__
#define __KERNEL_PRINTK__


#include <linux/stdarg.h>
#include <linux/init.h>
#include <linux/kern_levels.h>

struct va_format {
	const char *fmt;
	va_list *va;
};

#define __printk_index_emit(...) do {} while (0)

/*
 * Some subsystems have their own custom printk that applies a va_format to a
 * generic format, for example, to include a device number or other metadata
 * alongside the format supplied by the caller.
 *
 * In order to store these in the way they would be emitted by the printk
 * infrastructure, the subsystem provides us with the start, fixed string, and
 * any subsequent text in the format string.
 *
 * We take a variable argument list as pr_fmt/dev_fmt/etc are sometimes passed
 * as multiple arguments (eg: `"%s: ", "blah"`), and we must only take the
 * first one.
 *
 * subsys_fmt_prefix must be known at compile time, or compilation will fail
 * (since this is a mistake). If fmt or level is not known at compile time, no
 * index entry will be made (since this can legitimately happen).
 */
#define printk_index_subsys_emit(subsys_fmt_prefix, level, fmt, ...) \
	__printk_index_emit(fmt, level, subsys_fmt_prefix)


/**
 * pr_fmt - used by the pr_*() macros to generate the printk format string
 * @fmt: format string passed from a pr_*() macro
 *
 * This macro can be used to generate a unified format string for pr_*()
 * macros. A common use is to prefix all pr_*() messages in a file with a common
 * string. For example, defining this at the top of a source file:
 *
 *        #define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
 *
 * would prefix all pr_info, pr_emerg... messages in the file with the module
 * name.
 */
#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif
#define printk(fmt, ...)

/*
 * Dummy printk for disabled debugging statements to use whilst maintaining
 * gcc's format checking.
 */
#define no_printk(fmt, ...)				\
({							\
	if (0)						\
		printk(fmt, ##__VA_ARGS__);		\
	0;						\
})

#define pr_warn_once(fmt, ...)

/**
 * pr_debug - Print a debug-level message conditionally
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to dynamic_pr_debug() if CONFIG_DYNAMIC_DEBUG is
 * set. Otherwise, if DEBUG is defined, it's equivalent to a printk with
 * KERN_DEBUG loglevel. If DEBUG is not defined it does nothing.
 *
 * It uses pr_fmt() to generate the format string (dynamic_pr_debug() uses
 * pr_fmt() internally).
 */
#define pr_debug(fmt, ...) \
	printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)

/**
 * pr_emerg - Print an emergency-level message
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a printk with KERN_EMERG loglevel. It uses pr_fmt() to
 * generate the format string.
 */
#define pr_emerg(fmt, ...) \
	printk(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
/**
 * pr_alert - Print an alert-level message
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a printk with KERN_ALERT loglevel. It uses pr_fmt() to
 * generate the format string.
 */
#define pr_alert(fmt, ...) \
	printk(KERN_ALERT pr_fmt(fmt), ##__VA_ARGS__)
/**
 * pr_crit - Print a critical-level message
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a printk with KERN_CRIT loglevel. It uses pr_fmt() to
 * generate the format string.
 */
#define pr_crit(fmt, ...) \
	printk(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__)
/**
 * pr_err - Print an error-level message
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a printk with KERN_ERR loglevel. It uses pr_fmt() to
 * generate the format string.
 */
#define pr_err(fmt, ...) \
	printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
/**
 * pr_warn - Print a warning-level message
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a printk with KERN_WARNING loglevel. It uses pr_fmt()
 * to generate the format string.
 */
#define pr_warn(fmt, ...) \
	printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
/**
 * pr_notice - Print a notice-level message
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a printk with KERN_NOTICE loglevel. It uses pr_fmt() to
 * generate the format string.
 */
#define pr_notice(fmt, ...) \
	printk(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
/**
 * pr_info - Print an info-level message
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a printk with KERN_INFO loglevel. It uses pr_fmt() to
 * generate the format string.
 */
#define pr_info(fmt, ...) \
	printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info_once(fmt, ...) \
	printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)

/**
 * pr_cont - Continues a previous log message in the same line.
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a printk with KERN_CONT loglevel. It should only be
 * used when continuing a log message with no newline ('\n') enclosed. Otherwise
 * it defaults back to KERN_DEFAULT loglevel.
 */
#define pr_cont(fmt, ...) \
	printk(KERN_CONT fmt, ##__VA_ARGS__)

/**
 * pr_devel - Print a debug-level message conditionally
 * @fmt: format string
 * @...: arguments for the format string
 *
 * This macro expands to a printk with KERN_DEBUG loglevel if DEBUG is
 * defined. Otherwise it does nothing.
 *
 * It uses pr_fmt() to generate the format string.
 */
#ifdef DEBUG
#define pr_devel(fmt, ...) \
	printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#else
#define pr_devel(fmt, ...) \
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#endif


#endif