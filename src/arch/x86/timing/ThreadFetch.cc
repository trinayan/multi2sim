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

#include "Cpu.h"
#include "Timing.h"
#include "Thread.h"
#include "TraceCache.h"


namespace x86
{

Thread::FetchStall Thread::canFetch()
{
	// There must be a context mapped to this thread
	if (!context)
		return FetchStallContext;

	// The context must be running
	if (!context->getState(Context::StateRunning))
		return FetchStallSuspended;

	// The current context must not have been sent an eviction signal
	if (context->evict_signal)
		return FetchStallContext;

	// Fetch queue must have not exceeded the limit of stored bytes to be
	// able to store new macro-instructions.
	if (fetch_queue_occupancy >= Cpu::getFetchQueueSize())
		return FetchStallFetchQueue;

    //Do not fetch if CPU is flushing the pipelines
    //Used for transitoning between timing simulator and emulator

	if(cpu->getFlushing())
	{
		std::cout << "Flushing";
		return FetchStallInvalid;
	}
	// If the next fetch address belongs to a new block, cache system
	// must be accessible to read it.

	unsigned block_address = fetch_neip & ~(instruction_module->getBlockSize() - 1);
	if (block_address != fetch_block_address)
	{
		mem::Mmu *mmu = context->getMmu();
		mem::Mmu::Space *mmu_space = context->getMmuSpace();
		unsigned physical_address = mmu->TranslateVirtualAddress(
				mmu_space,
				fetch_neip);
		if (!instruction_module->canAccess(physical_address))
			return FetchStallInstructionMemory;
	}

	// We can fetch
	return FetchStallUsed;
}


Uop *Thread::FetchInstruction(bool fetch_from_trace_cache)
{
	// A context must be mapped
	assert(context);

	// Advance current fetch instruction pointer
	fetch_eip = fetch_neip;

	// Record previous speculative mode
	bool previous_speculative_mode = context->getState(Context::StateSpecMode);

	// Force it in the emulator, entering speculative mode if necessary
	context->setEip(fetch_eip);

	// Record new speculative mode
	bool speculative_mode = context->getState(Context::StateSpecMode);

	// Run emulation
	context->Execute();

	// Set next fetch instruction pointer to the next instruction
	fetch_neip = fetch_eip + context->getInstruction()->getSize();

	// If no micro-instruction was generated by this instruction, create a
	// 'nop' micro-instruction. This makes sure that there is always a
	// micro-instruction representing the regular control flow of
	// macro-instructions of the program. This is important for the traces
	// stored in the trace cache.
	if (!context->getNumUinsts())
		context->newUinst(Uinst::OpcodeNop, 0, 0, 0, 0, 0, 0, 0);

	// Traverse micro-instructions created by the x86 emulator
	int num_uinsts = context->getNumUinsts();
	int uinst_index = 0;
	Uop *ret_uop = nullptr;
	while (context->getNumUinsts())
	{
		// Get micro-instruction from head of list
		std::shared_ptr<Uinst> uinst = context->ExtractUinst();

		// Create uop
		auto uop = misc::new_shared<Uop>(this,
				context,
				uinst);

		// Populate macro-instruction information
		uop->mop_count = num_uinsts;
		uop->mop_size = context->getInstruction()->getSize();
		uop->mop_id = uop->getId() - uinst_index;
		uop->mop_index = uinst_index;

		// Other fields
		uop->eip = fetch_eip;
		uop->from_trace_cache = fetch_from_trace_cache;
		uop->speculative_mode = speculative_mode;
		uop->fetch_address = fetch_address;
		uop->fetch_access = fetch_access;
		uop->neip = context->getRegs().getEip();
		uop->predicted_neip = fetch_neip;
		uop->target_neip = context->getTargetEip();

		// Record whether this uop is the first in spec mode
		uop->first_speculative_mode = uinst_index == 0 &&
				!previous_speculative_mode &&
				speculative_mode;

		// Calculate physical address of a memory access
		if (uop->getFlags() & Uinst::FlagMem)
		{
			mem::Mmu *mmu = context->getMmu();
			mem::Mmu::Space *mmu_space = context->getMmuSpace();
			uop->physical_address = mmu->TranslateVirtualAddress(
					mmu_space,
					uinst->getAddress());
		}

		// Trace
		if (Timing::trace)
		{
			// New instruction
			Timing::trace << misc::fmt("x86.new_inst "
					"id=%lld "
					"core=%d",
					uop->getIdInCore(),
					core->getId());

			// Speculative mode
			if (uop->speculative_mode)
				Timing::trace << " spec=\"t\"";

			// First instruction in speculative mode
			if (uop->first_speculative_mode)
				Timing::trace << " first_spec=\"t\"";

			// Macro-instruction disassembly
			if (!uinst_index)
				Timing::trace << " asm=\""
				<< *context->getInstruction()
				<< "\"";

			// Micro-instruction disassembly
			Timing::trace << " uasm=\""
					<< *uinst
					<< "\"";

			// Stage
			Timing::trace << " stg=\"fe\""
					<< '\n';
		}

		// Select as returned uop
		if (!ret_uop || (uop->getFlags() & Uinst::FlagCtrl))
			ret_uop = uop.get();

		// Insert into fetch queue
		InsertInFetchQueue(uop);

		// Stats
		cpu->incNumFetchedUinsts();
		num_fetched_uinsts++;
		if (fetch_from_trace_cache)
			trace_cache->incNumFetchedUinsts();

		// Next micro-instruction
		uinst_index++;
	}

	// Return first uop, or last branch uop if any.
	return ret_uop;
}


bool Thread::FetchFromTraceCache()
{
	// No room in trace cache queue
	assert(TraceCache::isPresent());
	if (trace_cache_queue_occupancy >= TraceCache::getQueueSize())
		return false;

	// Access BTB, branch predictor, and trace cache
	unsigned eip_branch = branch_predictor->getNextBranch(fetch_neip,
			instruction_module->getBlockSize());
	int prediction = eip_branch ?
			branch_predictor->LookupMultiple(eip_branch,
					TraceCache::getMaxBranches()) :
					0;
	TraceCache::Entry *entry;
	unsigned neip;
	bool hit = trace_cache->Lookup(fetch_neip,
			prediction,
			entry,
			neip);
	if (!hit)
		return 0;

	// Fetch instruction in trace cache line.
	for (int i = 0; i < entry->getNumMacroInstructions(); i++)
	{
		// If instruction caused context to suspend or finish
		if (!context->getState(Context::StateRunning))
			break;

		// Insert decoded uops into the trace cache queue. In the
		// simulation, the uop is inserted into the fetch queue, but its
		// occupancy is not increased.
		fetch_neip = entry->getMacroInstruction(i);
		Uop *uop = FetchInstruction(true);

		// No uop was produced by this macro-instruction
		if (!uop)
			continue;

		// If instruction is a branch, access branch predictor just in
		// order to have the necessary information to update it at commit.
		if (uop->getFlags() & Uinst::FlagCtrl)
		{
			branch_predictor->Lookup(uop);
			uop->predicted_neip = i == entry->getNumMacroInstructions() - 1 ?
					neip : entry->getMacroInstruction(i + 1);
		}
	}

	// Set next fetch address as returned by the trace cache, and exit.
	fetch_neip = neip;
	return true;
}


void Thread::Fetch()
{
	// Sanity
	assert(context);

	// Try to fetch from trace cache first
	if (TraceCache::isPresent() && FetchFromTraceCache())
		return;

	// If new block to fetch is not the same as the previously fetched (and
	// stored) block, access the instruction cache.
	unsigned block_address = fetch_neip & ~(instruction_module->getBlockSize() - 1);
	if (block_address != fetch_block_address)
	{
		// Translate address
		mem::Mmu *mmu = context->getMmu();
		mem::Mmu::Space *mmu_space = context->getMmuSpace();
		unsigned physical_address = mmu->TranslateVirtualAddress(
				mmu_space,
				fetch_neip);

		// Save last fetched block
		fetch_block_address = block_address;
		fetch_address = physical_address;

		// Access instruction cache
		assert(instruction_module->canAccess(physical_address));
		fetch_access = instruction_module->Access(
				mem::Module::AccessLoad,
				physical_address);

		// Stats
		num_btb_reads++;
	}

	// Fetch all instructions within the block up to the first predict-taken
	// branch
	while ((fetch_neip & ~(instruction_module->getBlockSize() - 1))
			== block_address)
	{
		// If instruction caused context to suspend or finish
		if (!context->getState(Context::StateRunning))
			break;

		// If fetch queue is full, stop fetching
		if (fetch_queue_occupancy >= Cpu::getFetchQueueSize())
			break;

		// Insert macro-instruction into the fetch queue. Since the
		// macro-instruction information is only available at this
		// point, we use it to decode instruction now and insert uops
		// into the fetch queue. However, the fetch queue occupancy is
		// increased with the macro-instruction size.
		Uop *uop = FetchInstruction(false);

		// Invalid x86 instruction, no forward progress in loop
		if (!context->getInstruction()->getSize())
			break;

		// No uop was produced by this macro-instruction
		if (!uop)
			continue;

		// Instructions detected as branches by the BTB are checked for
		// branch direction in the branch predictor. If they are
		// predicted taken, stop fetching from this block and set new
		// fetch address.
		if (uop->getFlags() & Uinst::FlagCtrl)
		{
			// Look up BTB
			unsigned target = branch_predictor->LookupBtb(uop);

			// Look up branch predictor
			BranchPredictor::Prediction prediction =
					branch_predictor->Lookup(uop);
			bool taken = prediction == BranchPredictor::PredictionTaken
					&& target;

			// Set next instruction pointer
			if (taken)
			{
				fetch_neip = target;
				uop->predicted_neip = target;
				break;
			}
		}
	}
}

}
