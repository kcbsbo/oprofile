/**
 * @file op_cpu_type.h
 * CPU type determination
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author Philippe Elie
 */

#ifndef OP_CPU_TYPE_H
#define OP_CPU_TYPE_H

#ifdef __cplusplus
extern "C" {
#endif

/** supported cpu type */
typedef enum {
	CPU_NO_GOOD = -1, /**< unsupported CPU type */
	CPU_PPRO, /**< Pentium Pro */
	CPU_PII, /**< Pentium II series */
	CPU_PIII, /**< Pentium III series */
	CPU_ATHLON, /**< AMD P6 series */
	CPU_TIMER_INT, /**< CPU using the timer interrupt */
	CPU_RTC, /**< other CPU to use the RTC */
	CPU_P4,  /**< Pentium 4 / Xeon series */
	CPU_IA64, /**< Generic IA64 */
	CPU_IA64_1, /**< IA64 Merced */
	CPU_IA64_2, /**< IA64 McKinley */
	CPU_HAMMER, /**< AMD Hammer family */
	CPU_P4_HT2, /**< Pentium 4 / Xeon series with 2 hyper-threads */
	CPU_AXP_EV4, /**< Alpha EV4 family */
	CPU_AXP_EV5, /**< Alpha EV5 family */
	CPU_AXP_PCA56, /**< Alpha PCA56 family */
	CPU_AXP_EV6, /**< Alpha EV6 family */
	CPU_AXP_EV67, /**< Alpha EV67 family */
	CPU_P6_MOBILE, /**< Pentium M series */
	MAX_CPU_TYPE
} op_cpu;

/**
 * get the CPU type from the kernel
 *
 * returns CPU_NO_GOOD if the CPU could not be identified.
 * This function can not work if the module is not loaded
 */
op_cpu op_get_cpu_type(void);

/**
 * get the cpu number based on string
 * @param cpu_string with either the cpu type identifier or cpu type number
 *
 * The function returns CPU_NO_GOOD if no matching string was found.
 */
op_cpu op_get_cpu_number(char const * cpu_string);

/**
 * get the cpu string.
 * @param cpu_type the cpu type identifier
 *
 * The function always return a valid char const * the core cpu denomination
 * or "invalid cpu type" if cpu_type is not valid.
 */
char const * op_get_cpu_type_str(op_cpu cpu_type);

/**
 * op_get_cpu_name - get the cpu name
 * @param cpu_type  the cpu identifier name
 *
 * The function always return a valid char const *
 * Return the OProfile CPU name, e.g. "i386/pii"
 */
char const * op_get_cpu_name(op_cpu cpu_type);

/**
 * compute the number of counters available
 * @param cpu_type numeric processor type
 *
 * returns 0 if the CPU could not be identified
 */
int op_get_nr_counters(op_cpu cpu_type);

typedef enum {
	OP_INTERFACE_NO_GOOD = -1,
	OP_INTERFACE_24,
	OP_INTERFACE_26
} op_interface;

/**
 * get the INTERFACE used to communicate between daemon and the kernel
 *
 * returns OP_INTERFACE_NO_GOOD if the INTERFACE could not be identified.
 * This function will identify the interface as OP_INTERFACE_NO_GOOD if
 * the module is not loaded.
 */
op_interface op_get_interface(void);

#ifdef __cplusplus
}
#endif

#endif /* OP_CPU_TYPE_H */
