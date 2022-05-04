/*
 * Copyright 2015-2017 Google, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_USB_PD_BDO_H
#define __LINUX_USB_PD_BDO_H

/* BDO : BIST Data Object */
#define BDO_MODE_RECV		((uint32_t)0 << 28)
#define BDO_MODE_TRANSMIT	((uint32_t)1 << 28)
#define BDO_MODE_COUNTERS	((uint32_t)2 << 28)
#define BDO_MODE_CARRIER0	((uint32_t)3 << 28)
#define BDO_MODE_CARRIER1	((uint32_t)4 << 28)
#define BDO_MODE_CARRIER2	((uint32_t)5 << 28)
#define BDO_MODE_CARRIER3	((uint32_t)6 << 28)
#define BDO_MODE_EYE		((uint32_t)7 << 28)
#define BDO_MODE_TESTDATA	((uint32_t)8 << 28)

#define BDO_MODE_MASK(mode)	((mode) & 0xf0000000)

#endif
