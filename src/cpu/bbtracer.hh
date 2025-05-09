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

#ifndef __CPU_BBTRACER_HH__
#define __CPU_BBTRACER_HH__

#include <fstream>
#include <regex>
#include <string>
#include <unordered_map>

#include "base/callback.hh"
#include "base/types.hh"
#include "cpu/static_inst.hh"
#include "cpu/thread_context.hh"
#include "sim/insttracer.hh"

namespace gem5
{

// Forward declarations
struct BBTracerParams;

namespace trace
{

// Forward declaration of BBTracer class
class BBTracer;

/**
 * Basic Block Tracer Record class
 * 
 * This class extends the InstRecord class to add functionality for
 * tracking basic blocks in a program.
 */
class BBTracerRecord : public InstRecord
{
  public:
    BBTracerRecord(Tick _when, ThreadContext *_thread,
                  const StaticInstPtr _staticInst, const PCStateBase &_pc,
                  BBTracer &_tracer,
                  const StaticInstPtr _macroStaticInst = nullptr)
        : InstRecord(_when, _thread, _staticInst, _pc, _macroStaticInst),
          tracer(_tracer)
    {}

    void dump();
    
    void setCPSeq(InstSeqNum seq);

    // // Override only the setData method used by lea instructions
    // void setData(const RegClass &reg_class, RegVal val);

    // void setData(const RegClass &reg_class, const void *val);


  protected:
    BBTracer &tracer;
    
    // Helper method to check for basic block markers
    void update();
};

/**
 * Basic Block Tracer class
 * 
 * This class tracks the execution of basic blocks in a program.
 * It detects instructions that mark the beginning of a basic block
 * by looking for lea instructions that reference a string with the format
 * "bbid#functionname#bbname" and records execution counts and times for
 * each basic block.
 */
class BBTracer : public InstTracer
{
  public:
    typedef BBTracerParams Params;
    BBTracer(const BBTracerParams &params);

    InstRecord *getInstRecord(Tick when, ThreadContext *tc,
                             const StaticInstPtr staticInst, const PCStateBase &pc,
                             const StaticInstPtr macroStaticInst = nullptr) override;

    /**
     * Disable the map between register id and basic block id
     * @param instRecord The instruction record
     */
    void disableRegToBBId(InstRecord *instRecord);

    /**
     * Update the map between register id and basic block id
     * @param instRecord The instruction record
     * @param bbid The basic block id
     */
    void updateRegToBBId(InstRecord *instRecord, const std::string &bbid);

    /**
     * Check if the instruction uses a register that is mapped to a basic block
     * @param instRecord The instruction record
     * @return The basic block id if the instruction uses a register that is mapped to a basic block, std::nullopt otherwise
     */
    std::optional<std::string> usesBBMarker(InstRecord *instRecord);

    /**
     * Record a basic block execution
     * @param bbid The basic block ID
     */
    void recordBBExecution(const std::string &bbid);

    /**
     * Write the profiling results to a file
     */
    void writeResults();

    /**
     * Increment the instruction count for the current basic block
     */
    void incrementInstCount();

    /**
     * Set the current time
     */
    void setCurrentTime(Tick when);

  private:
    /** Initialize the tracer */
    void initialize();

    /** Output file for profiling results */
    std::string outputFile;

    /** Input file for basic block operation counts */
    std::string opCountFile;

    /** Map between register id and basic block id */
    mutable std::unordered_map<RegId, std::string> regToBBId;

    /** Map of basic block IDs to execution counts */
    mutable std::unordered_map<std::string, uint64_t> bbCounts;

    /** Map of basic block IDs to total execution time (in ticks) */
    mutable std::unordered_map<std::string, Tick> bbTimes;

    /** Map of basic block IDs to instruction counts */
    mutable std::unordered_map<std::string, uint64_t> bbInstCounts;

    /** Map of basic block IDs to operation counts */
    mutable std::unordered_map<std::string, uint64_t> bbOpCounts;

    /** Last seen basic block ID */
    mutable std::string lastBBId;

    /** Timestamp of last basic block entry */
    mutable Tick lastBBTime;

    /** Current instruction count since last basic block */
    mutable uint64_t currentInstCount;

    /** Current time */
    mutable Tick currentTime;
};

} // namespace trace
} // namespace gem5

#endif // __CPU_BBTRACER_HH__ 