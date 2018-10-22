/*
 * Partial header file imported from the linux kernel
 * (arch/x86/include/asm/msr-index.h)
 * as it is not provided by the kernel sources anymore
 *
 * Only the minimal blocks of macro have been included
 * ----
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * ----
 */

#ifndef _ASM_X86_MSR_INDEX_H
#define _ASM_X86_MSR_INDEX_H

/*
 * CPU model specific register (MSR) numbers.
 *
 * Do not add new entries to this file unless the definitions are shared
 * between multiple compilation units.
 */

/* Intel MSRs. Some also available on other CPUs */

/* C-state Residency Counters */
#define MSR_PKG_C3_RESIDENCY 0x000003f8
#define MSR_PKG_C6_RESIDENCY 0x000003f9
#define MSR_ATOM_PKG_C6_RESIDENCY 0x000003fa
#define MSR_PKG_C7_RESIDENCY 0x000003fa
#define MSR_CORE_C3_RESIDENCY 0x000003fc
#define MSR_CORE_C6_RESIDENCY 0x000003fd
#define MSR_CORE_C7_RESIDENCY 0x000003fe
#define MSR_KNL_CORE_C6_RESIDENCY 0x000003ff
#define MSR_PKG_C2_RESIDENCY 0x0000060d
#define MSR_PKG_C8_RESIDENCY 0x00000630
#define MSR_PKG_C9_RESIDENCY 0x00000631
#define MSR_PKG_C10_RESIDENCY 0x00000632

/* Run Time Average Power Limiting (RAPL) Interface */

#define MSR_RAPL_POWER_UNIT 0x00000606

#define MSR_PKG_POWER_LIMIT 0x00000610
#define MSR_PKG_ENERGY_STATUS 0x00000611
#define MSR_PKG_PERF_STATUS 0x00000613
#define MSR_PKG_POWER_INFO 0x00000614

#define MSR_DRAM_POWER_LIMIT 0x00000618
#define MSR_DRAM_ENERGY_STATUS 0x00000619
#define MSR_UNCORE_FREQ_SCALING 0x00000621
#define MSR_DRAM_PERF_STATUS 0x0000061b
#define MSR_DRAM_POWER_INFO 0x0000061c

#define MSR_PP0_POWER_LIMIT 0x00000638
#define MSR_PP0_ENERGY_STATUS 0x00000639
#define MSR_PP0_POLICY 0x0000063a
#define MSR_PP0_PERF_STATUS 0x0000063b

#define MSR_PP1_POWER_LIMIT 0x00000640
#define MSR_PP1_ENERGY_STATUS 0x00000641
#define MSR_PP1_POLICY 0x00000642

/* Intel defined MSRs. */
#define MSR_IA32_TSC 0x00000010
#define MSR_SMI_COUNT 0x00000034

#define MSR_IA32_MPERF 0x000000e7
#define MSR_IA32_APERF 0x000000e8

#define MSR_IA32_THERM_STATUS 0x0000019c

#define MSR_IA32_MISC_ENABLE 0x000001a0
#define MSR_IA32_TEMPERATURE_TARGET 0x000001a2

#define MSR_IA32_PACKAGE_THERM_STATUS 0x000001b1

#endif /* _ASM_X86_MSR_INDEX_H */
