/*
 *  Multi2Sim
 *  Copyright (C) 2012  Rafael Ubal (ubal@ece.neu.edu)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef DRIVER_OPENCL_OPENCL_H
#define DRIVER_OPENCL_OPENCL_H

#include <driver/common/driver.h>
#include <lib/util/class.h>


/*
 * Class 'CLDriver'
 */

CLASS_BEGIN(OpenclDriver, Driver)

CLASS_END(OpenclDriver)

void OpenclDriverCreate(OpenclDriver *self);
void OpenclDriverDestroy(OpenclDriver *self);


/*
 * Public
 */

#define opencl_debug(...) debug(opencl_debug_category, __VA_ARGS__)
extern int opencl_debug_category;

int opencl_abi_call(X86Context *ctx);

void opencl_init(void);
void opencl_done(void);

void opencl_si_request_work(X86Emu *emu);

#endif

