/*
 * Ultra Wide Band
 * Debug Support
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef __UWB_BUGS_MACROS_H__
#define __UWB_BUGS_MACROS_H__

/**
 * Macros for the testing, enabling, or disabling of bug workarounds
 *
 * Example:
 * in header file:
 *     #define UWB_BUG_43 1 // to enable the bug workaround
 * in code:
 *     ...
 *     // some random condition enables the workaround
 *     if (1)
 *             BUG_SET(UWB_BUG_43, kk.uwb_bug_43, 1);
 *     if (BUG_READ(UWB_BUG_43, kk.uwb_bug_43))
 *             printf("bug wa enabled\n");
 *     else
 *             printf("bug wa disabled\n");
 *     ...
 */
#define __BUG_READ_0(b) 0
#define __BUG_READ_1(b) b
#define __BUG_READ(a, b) __BUG_READ_##a(b)
#define BUG_READ(a, b) __BUG_READ(a, (b))

#define __BUG_SET_0(b) do { } while(0)
#define __BUG_SET_1(b) do { (b) = 1; } while(0)
#define __BUG_SET(a, b) __BUG_SET_##a(b)
#define BUG_SET(a, b) __BUG_SET(a, b)

#define __BUG_RESET_0(b) do { } while(0)
#define __BUG_RESET_1(b) do { (b) = 0; } while(0)
#define __BUG_RESET(a, b) __BUG_RESET_##a(b)
#define BUG_RESET(a, b) __BUG_RESET(a, b)

#define __BUG_ASSIGN_0(b, c) do { } while(0)
#define __BUG_ASSIGN_1(b, c) do { (b) = (c); } while(0)
#define __BUG_ASSIGN(a, b, c) __BUG_ASSIGN_##a(b, c)
#define BUG_ASSIGN(a, b, c) __BUG_ASSIGN(a, b, c)

#endif /* #ifndef __UWB_BUGS_MACROS_H__ */
