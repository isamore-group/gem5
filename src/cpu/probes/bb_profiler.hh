/*
 * Copyright (c) 2024 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __CPU_PROBES_BB_PROFILER_HH__
#define __CPU_PROBES_BB_PROFILER_HH__

#include <fstream>
#include <map>
#include <string>
#include <unordered_map>

#include "base/types.hh"
#include "cpu/static_inst.hh"
#include "debug/BBProfiler.hh"
#include "params/BBProfiler.hh"
#include "sim/probe/probe.hh"
#include "sim/sim_exit.hh"

namespace gem5
{

/**
 * Basic Block Profiler class
 * 
 * This class tracks the execution of basic blocks in a program.
 * It detects instructions that mark the beginning of a basic block
 * by looking for the pattern "movq bbid#..." and records execution
 * counts and times for each basic block.
 */
class BBProfiler : public ProbeListenerObject
{
  public:
    BBProfiler(const BBProfilerParams &params);
    ~BBProfiler();

    /** Setup the probe listeners */
    virtual void regProbeListeners() override;

    /**
     * Handler for the CommittedInst probe point
     * This is called for every committed instruction
     */
    void committedInstHandler(const std::pair<const StaticInstPtr, Addr> &inst_pc);

    /**
     * Check if an instruction is a basic block marker
     * Looks for the pattern "movq bbid#..." in the disassembly
     */
    bool isBBMarkerInst(const StaticInstPtr &inst, std::string &bbid);

    /**
     * Write the profiling results to a file
     */
    void writeResults();

  private:
    typedef ProbeListenerArg<BBProfiler, std::pair<const StaticInstPtr, Addr>> BBProfilerListener;

    /** Output file for profiling results */
    std::string outputFile;

    /** Map of basic block IDs to execution counts */
    std::unordered_map<std::string, uint64_t> bbCounts;

    /** Map of basic block IDs to total execution time (in ticks) */
    std::unordered_map<std::string, Tick> bbTimes;

    /** Last seen basic block ID */
    std::string lastBBId;

    /** Timestamp of last basic block entry */
    Tick lastBBTime;

    /** Whether the profiler is currently active */
    bool active;
};

} // namespace gem5

#endif // __CPU_PROBES_BB_PROFILER_HH__ 