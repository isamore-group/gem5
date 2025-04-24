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

#include "cpu/bbtracer.hh"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>

#include "arch/generic/mmu.hh"
#include "arch/x86/regs/int.hh"
#include "base/callback.hh"
#include "base/debug.hh"
#include "base/logging.hh"
#include "base/statistics.hh"
#include "base/trace.hh"
#include "cpu/base.hh"
#include "cpu/reg_class.hh"
#include "cpu/static_inst.hh"
#include "cpu/thread_context.hh"
#include "debug/BBTracer.hh"
#include "debug/ExecAll.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "mem/translating_port_proxy.hh"
#include "params/BBTracer.hh"
#include "sim/core.hh"
#include "sim/faults.hh"
#include "sim/sim_exit.hh"

namespace gem5
{

namespace trace
{

void
BBTracerRecord::traceInst(const StaticInstPtr &inst, bool ran)
{
    // The basic block detection is now done in setData methods
    // This method is kept for compatibility but does minimal work
    
    // Increment the instruction count for the current basic block
    if (ran) {
        tracer.incrementInstCount();
    }
}

void
BBTracerRecord::dump()
{
    // We don't need to dump anything for the BB tracer
}

// Helper method to check for basic block markers
void
BBTracerRecord::checkForBBMarker()
{
    // Only check for lea instructions
    std::string disasm = staticInst->disassemble(pc->instAddr());

    if (disasm.find("lea") == std::string::npos) {
        return;
    }
    
    
    // We have a lea instruction and we're in setData, so we have the result
    // The result is the effective address calculated by the lea instruction
    // This address should point to our bbid string

    // Get the data (address) from the instruction result
    Addr targetAddr = data.asInt;  // For lea instructions, the result is stored as an integer

    // Skip if the address is invalid or too small
    if (targetAddr < 1000) {
        return;
    }
    
    // Try to read the memory at the target address
    const int maxStringLength = 256;
    uint8_t buffer[maxStringLength];
    memset(buffer, 0, maxStringLength);
    
    TranslatingPortProxy proxy(thread);
    if (proxy.tryReadBlob(targetAddr, buffer, maxStringLength)) {
        // Check if the string starts with "bbid#"
        std::string str(reinterpret_cast<char*>(buffer));

        // the string should be like "____bbid#functionname#bbname"
        if (str.find("____bbid#") == 0) {
            // Extract the bbid (remove the "____bbid#" prefix)
            std::string bbid = str.substr(9);
            // Truncate at the first null character if present
            size_t nullPos = bbid.find('\0');
            if (nullPos != std::string::npos) {
                bbid = bbid.substr(0, nullPos);
            }
            
            if (debug::BBTracer) {
                DPRINTF(BBTracer, "Found bbid marker: %s at address %#x\n", 
                    bbid.c_str(), targetAddr);
            }
                    
            // Record the basic block execution
            tracer.recordBBExecution(bbid, when);
        }
    }
}

// Override only the setData method used by lea instructions
void
BBTracerRecord::setData(const RegClass &reg_class, RegVal val)
{
    // Call the parent class method first
    InstRecord::setData(reg_class, val);
    
    // Now check for basic block markers
    checkForBBMarker();
}

BBTracer::BBTracer(const BBTracerParams &params)
    : InstTracer(static_cast<const InstTracerParams &>(params)),
      outputFile(params.output_file),
      lastBBId(""),
      lastBBTime(0),
      currentInstCount(0)
{
    if (debug::BBTracer) {
        trace::getDebugLogger()->dprintf_flag(
            curTick(), name(), "BBTracer",
            "BBTracer created with output file: %s\n",
            outputFile);
    }

    // Register a callback to write results when simulation ends
    registerExitCallback([this]() { writeResults(); });
}

BBTracer::~BBTracer()
{
    // Make sure results are written when the tracer is destroyed
    writeResults();
}

InstRecord *
BBTracer::getInstRecord(Tick when, ThreadContext *tc,
                       const StaticInstPtr staticInst, const PCStateBase &pc,
                       const StaticInstPtr macroStaticInst)
{
    if (!debug::BBTracer)
        return nullptr;
    
    auto record = new BBTracerRecord(when, tc, staticInst, pc, *this, macroStaticInst);

    return record;
}

void
BBTracer::recordBBExecution(const std::string &bbid, Tick when) const
{
    DPRINTF(BBTracer, "Recording basic block execution: %s at time %d, inst count: %d\n", bbid.c_str(), when, currentInstCount);
    // Increment the count for this basic block
    bbCounts[bbid]++;

    // If we've seen a previous basic block, update its time and instruction count
    if (!lastBBId.empty()) {
        bbTimes[lastBBId] += (when - lastBBTime);
        bbInstCounts[lastBBId] += currentInstCount;
    }

    // Update the last seen basic block
    lastBBId = bbid;
    lastBBTime = when;
    
    // Reset the instruction counter for the new basic block
    currentInstCount = 0;
}

void
BBTracer::writeResults() const
{
    // If we've seen a previous basic block, update its time
    if (!lastBBId.empty()) {
        bbTimes[lastBBId] += (curTick() - lastBBTime);
    }

    // Open the output file
    std::ofstream outFile(outputFile);
    if (!outFile.is_open()) {
        warn("BBTracer: Could not open output file %s\n", outputFile);
        return;
    }

    // Write the header
    outFile << "Basic Block ID,Execution Count,Total Time (ticks),Average Time (ticks),Instruction Count,CPI\n";

    // Write the data for each basic block
    for (const auto &entry : bbCounts) {
        const std::string &bbid = entry.first;
        uint64_t count = entry.second;
        Tick totalTime = 0;
        uint64_t instCount = 0;
        
        // Get the total time for this basic block
        auto timeIt = bbTimes.find(bbid);
        if (timeIt != bbTimes.end()) {
            totalTime = timeIt->second;
        }
        
        // Get the instruction count for this basic block
        auto instIt = bbInstCounts.find(bbid);
        if (instIt != bbInstCounts.end()) {
            instCount = instIt->second;
        }
        
        double avgTime = count > 0 ? static_cast<double>(totalTime) / count : 0.0;
        double cpi = instCount > 0 ? static_cast<double>(totalTime) / instCount : 0.0;

        outFile << bbid << "," << count << "," << totalTime << "," << std::fixed
                << std::setprecision(2) << avgTime << "," << instCount << "," 
                << std::setprecision(2) << cpi << "\n";
    }

    outFile.close();
    inform("BBTracer: Results written to %s\n", outputFile);
}

// Add a method to increment the instruction count
void
BBTracer::incrementInstCount() const
{
    currentInstCount++;
}

} // namespace trace
} // namespace gem5 