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

#ifndef X86_ARCH_TIMING_UOP_H
#define X86_ARCH_TIMING_UOP_H

#include <arch/x86/emu/UInst.h>

namespace x86
{

// Forward declarations
class UInst;
class Context;
class Thread;

class Uop
{
private:

	// Micro-instruction 
	UInst *uinst;
	UInstFlag flags;

	// Name and sequence numbers 
	long long magic;  // Magic number for debugging 
	long long id;  // Unique ID 
	long long id_in_core;  // Unique ID in core 

	// Software context and hardware thread where uop belongs 
	Context *ctx;
	Thread *thread;

	// Fetch info 
	unsigned int eip;  // Address of x86 macro-instruction 
	unsigned int neip;  // Address of next non-speculative x86 macro-instruction 
	unsigned int pred_neip; // Address of next predicted x86 macro-instruction (for branches) 
	unsigned int target_neip;  // Address of target x86 macro-instruction assuming branch taken (for branches) 
	int specmode;
	unsigned int fetch_address;  // Physical address of memory access to fetch this instruction 
	long long fetch_access;  // Access identifier to fetch this instruction 
	int trace_cache;  // Flag telling if uop came from trace cache 

	// Fields associated with macroinstruction 
	char mop_name[40];
	int mop_index;  // Index of uop within macroinstruction 
	int mop_count;  // Number of uops within macroinstruction 
	int mop_size;  // Corresponding macroinstruction size 
	long long mop_id;  // Sequence number of macroinstruction 

	// Logical dependencies 
	int idep_count;
	int odep_count;

	// Physical mappings 
	int ph_int_idep_count, ph_fp_idep_count, ph_xmm_idep_count;
	int ph_int_odep_count, ph_fp_odep_count, ph_xmm_odep_count;
	int ph_idep[UInstMaxIDeps];
	int ph_odep[UInstMaxODeps];
	int ph_oodep[UInstMaxODeps];

	// Queues where instruction is 
	int in_fetch_queue;
	int in_uop_queue;
	int in_iq;
	int in_lq;
	int in_sq;
	int in_preq;
	int in_event_queue;
	int in_rob;
	int in_uop_trace_list;

	// Instruction status 
	int ready;
	int issued;
	int completed;

	// For memory uops 
	unsigned int phy_addr;  // ... corresponding to 'uop->uinst->address' 

	// Cycles 
	long long when;  // cycle when ready 
	long long issue_try_when;  // first cycle when f.u. is tried to be reserved 
	long long issue_when;  // cycle when issued 

	// Branch prediction 
	int pred;  // Global prediction (0=not taken, 1=taken) 
	int bimod_index, bimod_pred;
	int twolevel_bht_index, twolevel_pht_row, twolevel_pht_col, twolevel_pred;
	int choice_index, choice_pred;

public:

	/// Constructor
	Uop();

	/// Destructor
	~Uop();

	/// Return unique instance of the Uop
	Uop *getInstance();

	void FreeIfNotQueued();
	void Dump();

	bool Exists();
	void CountDeps();
};

}

#endif // X86_ARCH_TIMING_UOP_H