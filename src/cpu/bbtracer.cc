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
#include "base/callback.hh"
#include "base/debug.hh"
#include "base/logging.hh"
#include "base/statistics.hh"
#include "base/trace.hh"
#include "cpu/base.hh"
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
    // Check if this instruction is a basic block marker
    std::string bbid;
    if (tracer.isBBMarkerInst(inst, thread, pc->instAddr(), bbid)) {
        if (debug::BBTracer) {
            trace::getDebugLogger()->dprintf_flag(
                curTick(), name(), "BBTracer",
                "Found basic block marker: %s at PC %#x\n",
                bbid, pc->instAddr());
        }

        // Record the basic block execution
        tracer.recordBBExecution(bbid, when);
    }
}

void
BBTracerRecord::dump()
{
    // We don't need to dump anything for the BB tracer
}

BBTracer::BBTracer(const BBTracerParams &params)
    : InstTracer(static_cast<const InstTracerParams &>(params)),
      outputFile(params.output_file),
      lastBBId(""),
      lastBBTime(0)
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

    return new BBTracerRecord(when, tc, staticInst, pc, *this, macroStaticInst);
}

bool
BBTracer::isBBMarkerInst(const StaticInstPtr &inst, ThreadContext *tc, 
                         Addr pc, std::string &bbid) const
{
    // Get the disassembly of the instruction
    std::string disasm = inst->disassemble(pc);
    
    // Check if it's a lea instruction
    if (disasm.find("lea") == std::string::npos) {
        return false;
    }
    
    // For x86 lea instructions, we need to extract the target address
    // Example formats:
    // "lea    0x123456(%rip), %rax"
    // "leaq   symbol(%rip), %rax"
    
    // Extract the effective address from the lea instruction
    // This is a simplified approach and may need to be adjusted based on
    // the actual implementation of the CPU model
    
    // Try to extract the target address from the disassembly
    std::regex targetPattern(R"(lea[q]?\s+([^,]+),%r[a-z0-9]+)");
    std::smatch match;
    
    if (std::regex_search(disasm, match, targetPattern) && match.size() > 1) {
        std::string targetExpr = match[1].str();
        
        // Now we need to evaluate the target expression to get the actual address
        // This is architecture-specific and may require more complex parsing
        
        // For simplicity, let's assume the target is of the form "offset(%rip)"
        std::regex offsetPattern(R"((-?0x[0-9a-f]+|\d+)\(%rip\))");
        std::smatch offsetMatch;
        
        if (std::regex_search(targetExpr, offsetMatch, offsetPattern) && offsetMatch.size() > 1) {
            std::string offsetStr = offsetMatch[1].str();
            int64_t offset;
            
            // Convert the offset string to an integer
            if (offsetStr.find("0x") == 0) {
                // Hexadecimal offset
                offset = std::stoll(offsetStr, nullptr, 16);
            } else {
                // Decimal offset
                offset = std::stoll(offsetStr);
            }
            
            // Calculate the target address
            // The target address is PC + offset + size of the instruction
            // For x86, we need to know the size of the instruction
            // For simplicity, let's assume a fixed size of 7 bytes for lea instructions
            const int leaInstructionSize = 7;
            Addr targetAddr = pc + offset + leaInstructionSize;
            
            // Now read the memory at the target address to get the string
            // We need to read the memory in chunks until we find a null terminator
            const int maxStringLength = 256;
            uint8_t buffer[maxStringLength];
            memset(buffer, 0, maxStringLength);
            
            // Try to read the memory using the TranslatingPortProxy
            TranslatingPortProxy proxy(tc);
            if (proxy.tryReadBlob(targetAddr, buffer, maxStringLength)) {
                // Check if the string starts with "bbid#"
                std::string str(reinterpret_cast<char*>(buffer));
                if (str.find("bbid#") == 0) {
                    // Extract the bbid (remove the "bbid#" prefix)
                    bbid = str.substr(5);
                    // Truncate at the first null character if present
                    size_t nullPos = bbid.find('\0');
                    if (nullPos != std::string::npos) {
                        bbid = bbid.substr(0, nullPos);
                    }
                    return true;
                }
            }
        }
    }
    
    return false;
}

void
BBTracer::recordBBExecution(const std::string &bbid, Tick when) const
{
    // Increment the count for this basic block
    bbCounts[bbid]++;

    // If we've seen a previous basic block, update its time
    if (!lastBBId.empty()) {
        bbTimes[lastBBId] += (when - lastBBTime);
    }

    // Update the last seen basic block
    lastBBId = bbid;
    lastBBTime = when;
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
    outFile << "Basic Block ID,Execution Count,Total Time (ticks),Average Time (ticks)\n";

    // Write the data for each basic block
    for (const auto &entry : bbCounts) {
        const std::string &bbid = entry.first;
        uint64_t count = entry.second;
        Tick totalTime = 0;
        
        // Get the total time for this basic block
        auto timeIt = bbTimes.find(bbid);
        if (timeIt != bbTimes.end()) {
            totalTime = timeIt->second;
        }
        
        double avgTime = count > 0 ? static_cast<double>(totalTime) / count : 0.0;

        outFile << bbid << "," << count << "," << totalTime << "," << std::fixed
                << std::setprecision(2) << avgTime << "\n";
    }

    outFile.close();
    inform("BBTracer: Results written to %s\n", outputFile);
}

} // namespace trace
} // namespace gem5 