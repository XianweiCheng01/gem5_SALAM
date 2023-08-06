#include "hwacc/scratchpad_memory.hh"
#include "base/random.hh"
#include "base/trace.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "sim/system.hh"
#include "debug/Drain.hh"

#include <stdio.h>
#include <stdlib.h>
#include <iomanip>

using namespace std;

/***************************************************************************************
 * Scratchpad Scratchpad Device for Accelerators using CommMemInterface
 * Acts as a simple memory for external devices
 * Enables specialization of access for parent device
 **************************************************************************************/
#include "debug/MemoryAccess.hh"

ScratchpadMemory::ScratchpadMemory(const ScratchpadMemoryParams *p) :
    AbstractMemory(p),
    port(name() + ".port", *this), latency(p->latency),
    latency_var(p->latency_var), bandwidth(p->bandwidth),
    dequeueEvent([this]{ dequeue(); }, name()),
    readyMode(p->ready_mode),
    readOnInvalid(p->read_on_invalid),
    writeOnValid(p->write_on_valid),
    resetOnScratchpadRead(p->reset_on_scratchpad_read),
    initial(true) {
    ready = new bool[range.size()];
    if (readyMode) {
        for (auto i=0;i<range.size();i++) {
            ready[i] = false;
        }
    }
    // Each port has its own release and dequeue events, as well as signals
    // Adding these events and signals for ".port"
    const std::string releaseEventName = csprintf("%s_release[0]", name());
    releaseEvent.push_back(EventFunctionWrapper([this]{ release(); }, releaseEventName));
    releaseTick.push_back(0);
    // const std::string dequeueEventName = csprintf("%s_dequeue[0]", name());
    // dequeueEvent.push_back(EventFunctionWrapper([this]{ dequeue(); }, dequeueEventName));
    isBusy.push_back(false);
    retryReq.push_back(false);
    retryResp.push_back(false);
}

bool
ScratchpadMemory::isReady(Addr ad, size_t size, bool read) {
    if (!readyMode) {
        return true;
    } else if (read) {
        // We are reading. We can read if readOnInvalid or
        // if all segments are valid.
        if (readOnInvalid) return true;
        Addr start_offset = ad - range.start();
        Addr end_offset = start_offset + size;
        for (auto i=start_offset; i<end_offset; i++) {
            if (ready[i] == false) return false;
        }
    } else {
        // We are writing. We can write if writeOnValid or
        // if all segments are invalid.
        if (writeOnValid) return true;
        Addr start_offset = ad - range.start();
        Addr end_offset = start_offset + size;
        for (auto i=start_offset; i<end_offset; i++) {
            if (ready[i] == true) return false;
        }
    }
    return true;
}

void
ScratchpadMemory::setAllReady(bool r) {
    if (readyMode && !initial){
        for (auto i=0;i<range.size();i++) {
            ready[i] = r;
        }
    }
    initial = true;
}

static inline void
tracePacket(System *sys, const char *label, PacketPtr pkt)
{
    int size = pkt->getSize();
#if THE_ISA != NULL_ISA
    if (size == 1 || size == 2 || size == 4 || size == 8) {
        DPRINTF(MemoryAccess,"%s from %s of size %i on address %#x data "
                "%#x %c\n", label, sys->getMasterName(pkt->req->masterId()),
                size, pkt->getAddr(), pkt->getUintX(TheISA::GuestByteOrder),
                pkt->req->isUncacheable() ? 'U' : 'C');
        return;
    }
#endif
    DPRINTF(MemoryAccess, "%s from %s of size %i on address %#x %c\n",
            label, sys->getMasterName(pkt->req->masterId()),
            size, pkt->getAddr(), pkt->req->isUncacheable() ? 'U' : 'C');
    DDUMP(MemoryAccess, pkt->getConstPtr<uint8_t>(), pkt->getSize());
}

#if TRACING_ON
#   define TRACE_PACKET(A) tracePacket(system(), A, pkt)
#else
#   define TRACE_PACKET(A)
#endif

void
ScratchpadMemory::scratchpadAccess(PacketPtr pkt, bool validateAccess)
{
    initial = false;
    if (pkt->cacheResponding()) {
        DPRINTF(MemoryAccess, "Cache responding to %#llx: not responding\n",
                pkt->getAddr());
        return;
    }

    if (pkt->cmd == MemCmd::CleanEvict || pkt->cmd == MemCmd::WritebackClean) {
        DPRINTF(MemoryAccess, "CleanEvict  on 0x%x: not responding\n",
                pkt->getAddr());
      return;
    }

    assert(AddrRange(pkt->getAddr(),
                     pkt->getAddr() + (pkt->getSize() - 1)).isSubset(range));

    uint8_t *hostAddr = pmemAddr + pkt->getAddr() - range.start();

    if (pkt->cmd == MemCmd::SwapReq) {
        if (pkt->isAtomicOp()) {
            if (pmemAddr) {
                pkt->setData(hostAddr);
                (*(pkt->getAtomicOp()))(hostAddr);
            }
        } else {
            std::vector<uint8_t> overwrite_val(pkt->getSize());
            uint64_t condition_val64;
            uint32_t condition_val32;

            panic_if(!pmemAddr, "Swap only works if there is real memory " \
                     "(i.e. null=False)");

            bool overwrite_mem = true;
            // keep a copy of our possible write value, and copy what is at the
            // memory address into the packet
            pkt->writeData(&overwrite_val[0]);
            pkt->setData(hostAddr);

            if (pkt->req->isCondSwap()) {
                if (pkt->getSize() == sizeof(uint64_t)) {
                    condition_val64 = pkt->req->getExtraData();
                    overwrite_mem = !std::memcmp(&condition_val64, hostAddr,
                                                 sizeof(uint64_t));
                } else if (pkt->getSize() == sizeof(uint32_t)) {
                    condition_val32 = (uint32_t)pkt->req->getExtraData();
                    overwrite_mem = !std::memcmp(&condition_val32, hostAddr,
                                                 sizeof(uint32_t));
                } else
                    panic("Invalid size for conditional read/write\n");
            }

            if (overwrite_mem)
                std::memcpy(hostAddr, &overwrite_val[0], pkt->getSize());

            assert(!pkt->req->isInstFetch());
            TRACE_PACKET("Read/Write");
            stats.numOther[pkt->req->masterId()]++;
        }
    } else if (pkt->isRead()) {
        assert(!pkt->isWrite());
        if (pkt->isLLSC()) {
            assert(!pkt->fromCache());
            // if the packet is not coming from a cache then we have
            // to do the LL/SC tracking here
            trackLoadLocked(pkt);
        }
        if (validateAccess) {
            if (!isReady(pkt->getAddr(),pkt->getSize(), true)) {
                panic("Scratchpad read at address: 0x%lx is invalid! Sector has not been written yet!\n", pkt->getAddr());
            }
            if (resetOnScratchpadRead) {
                Addr start_offset = pkt->getAddr() - range.start();
                Addr end_offset = start_offset + pkt->getSize();
                for (auto i=start_offset; i<end_offset; i++) {
                    ready[i] = false;
                }
            }
        }
        if (pmemAddr) {
            pkt->setData(hostAddr);
        }
        TRACE_PACKET(pkt->req->isInstFetch() ? "IFetch" : "Read");
        stats.numReads[pkt->req->masterId()]++;
        stats.bytesRead[pkt->req->masterId()] += pkt->getSize();
        if (pkt->req->isInstFetch())
            stats.bytesInstRead[pkt->req->masterId()] += pkt->getSize();
    } else if (pkt->isInvalidate() || pkt->isClean()) {
        assert(!pkt->isWrite());
        // in a fastmem system invalidating and/or cleaning packets
        // can be seen due to cache maintenance requests

        // no need to do anything
    } else if (pkt->isWrite()) {
        if (writeOK(pkt)) {
            if (pmemAddr) {
                pkt->writeData(hostAddr);
                DPRINTF(MemoryAccess, "%s wrote %i bytes to address %x\n",
                        __func__, pkt->getSize(), pkt->getAddr());
            }
            assert(!pkt->req->isInstFetch());
            TRACE_PACKET("Write");
            stats.numWrites[pkt->req->masterId()]++;
            stats.bytesWritten[pkt->req->masterId()] += pkt->getSize();
        }
        if (validateAccess) {
            if (!isReady(pkt->getAddr(),pkt->getSize(), false)) {
                panic("Scratchpad write at address: 0x%lx is invalid! Sector has not been cleared yet!\n", pkt->getAddr());
            }
        }
        // Set ready bits on external writes
        if (readyMode) {
            Addr start_offset = pkt->getAddr() - range.start();
            Addr end_offset = start_offset + pkt->getSize();
            for (auto i=start_offset; i<end_offset; i++) {
                ready[i] = true;
            }
        }
    } else {
        panic("Unexpected packet %s", pkt->print());
    }

    if (pkt->needsResponse()) {
        pkt->makeResponse();
    }
}

void
ScratchpadMemory::init()
{
    // allow unconnected memories as this is used in several ruby
    // systems at the moment
    if (port.isConnected()) {
        port.sendRangeChange();
    }
    initial = true;
}

Tick
ScratchpadMemory::recvAtomic(PacketPtr pkt, bool validateAccess)
{
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    scratchpadAccess(pkt, validateAccess);
    return getLatency();
}

Tick
ScratchpadMemory::recvAtomicBackdoor(PacketPtr pkt, MemBackdoorPtr &_backdoor)
{
    Tick latency = recvAtomic(pkt);

    if (backdoor.ptr())
        _backdoor = &backdoor;
    return latency;
}

void
ScratchpadMemory::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());

    functionalAccess(pkt);

    bool done = false;
    auto p = packetQueue.begin();
    // potentially update the packets in our packet queue as well
    while (!done && p != packetQueue.end()) {
        done = pkt->trySatisfyFunctional(p->pkt);
        ++p;
    }

    pkt->popLabel();
}

bool
ScratchpadMemory::recvTimingReq(PacketPtr pkt, PortID recvPort, bool validateAccess)
{
    panic_if(pkt->cacheResponding(), "Should not see packets where cache "
             "is responding");

    panic_if(!(pkt->isRead() || pkt->isWrite()),
             "Should only see read and writes at memory controller, "
             "saw %s to %#llx\n", pkt->cmdString(), pkt->getAddr());

    PortID idx = recvPort + 1;

    // we should not get a new request after committing to retry the
    // current one, but unfortunately the CPU violates this rule, so
    // simply ignore it for now
    if (retryReq[idx])
        return false;

    // if we are busy with a read or write, remember that we have to
    // retry
    if (isBusy[idx]) {
        retryReq[idx] = true;
        return false;
    }

    // technically the packet only reaches us after the header delay,
    // and since this is a memory controller we also need to
    // deserialise the payload before performing any write operation
    Tick receive_delay = pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;

    // update the release time according to the bandwidth limit, and
    // do so with respect to the time it takes to finish this request
    // rather than long term as it is the short term data rate that is
    // limited for any real memory

    // calculate an appropriate tick to release to not exceed
    // the bandwidth limit
    Tick duration = pkt->getSize() * bandwidth;

    // only consider ourselves busy if there is any need to wait
    // to avoid extra events being scheduled for (infinitely) fast
    // memories
    if (duration != 0) {
        schedule(releaseEvent[idx], curTick() + duration);
        releaseTick[idx] = curTick() + duration;
        isBusy[idx] = true;
    }

    // go ahead and deal with the packet and put the response in the
    // queue if there is one
    bool needsResponse = pkt->needsResponse();
    recvAtomic(pkt, validateAccess);
    // turn packet around to go back to requester if response expected
    if (needsResponse) {
        // recvAtomic() should already have turned packet into
        // atomic response
        assert(pkt->isResponse());

        Tick when_to_send = curTick() + receive_delay + getLatency();

        // typically this should be added at the end, so start the
        // insertion sort with the last element, also make sure not to
        // re-order in front of some existing packet with the same
        // address, the latter is important as this memory effectively
        // hands out exclusive copies (shared is not asserted)
        auto i = packetQueue.end();
        --i;
        while (i != packetQueue.begin() && when_to_send < i->tick &&
               !i->pkt->matchAddr(pkt))
            --i;

        // emplace inserts the element before the position pointed to by
        // the iterator, so advance it one step
        packetQueue.emplace(++i, pkt, when_to_send, idx);

        if (!retryResp[idx] && !dequeueEvent.scheduled()) {
            schedule(dequeueEvent, packetQueue.back().tick);
            // dequeueTick[idx] = packetQueue.back().tick;
        }
    } else {
        pendingDelete.reset(pkt);
    }

    return true;
}

void
ScratchpadMemory::release()
{
    // Tick now = curTick();
    unsigned idx;
    for (idx = 0; idx < isBusy.size(); idx++) {
        if ((!releaseEvent[idx].scheduled()) && (isBusy[idx])) {
            assert(isBusy[idx]);
            isBusy[idx] = false;
            if (retryReq[idx]) {
                retryReq[idx] = false;
                if (idx==0)
                    port.sendRetryReq();
                else
                    spm_ports[idx-1]->sendRetryReq();
            }
        }
    }
}

void
ScratchpadMemory::dequeue()
{
    assert(!packetQueue.empty());
    DeferredPacket deferred_pkt = packetQueue.front();
    PortID idx = deferred_pkt.origin;
    if (idx == 0)
        retryResp[idx] = !port.sendTimingResp(deferred_pkt.pkt);
    else
        retryResp[idx] = !spm_ports[idx-1]->sendTimingResp(deferred_pkt.pkt);

    if (!retryResp[idx]) {
        packetQueue.pop_front();

        // if the queue is not empty, schedule the next dequeue event,
        // otherwise signal that we are drained if we were asked to do so
        if (!packetQueue.empty()) {
            // if there were packets that got in-between then we
            // already have an event scheduled, so use re-schedule
            reschedule(dequeueEvent,
                       std::max(packetQueue.front().tick, curTick()), true);
        } else if (drainState() == DrainState::Draining) {
            DPRINTF(Drain, "Draining of ScratchpadMemory complete\n");
            signalDrainDone();
        }
    }
}

Tick
ScratchpadMemory::getLatency() const
{
    return latency +
        (latency_var ? random_mt.random<Tick>(0, latency_var) : 0);
}

void
ScratchpadMemory::recvRespRetry(PortID id)
{
    PortID idx = id+1;
    assert(retryResp[idx]);

    dequeue();
}

Port &
ScratchpadMemory::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "port") {
        return port;
    } else if (if_name == "spm_ports") {
        if (idx >= spm_ports.size()) {
            spm_ports.resize((idx+1), nullptr);
            const std::string releaseEventName = csprintf("%s_release[%d]", name(), (idx+1));
            releaseEvent.resize((idx+2), EventFunctionWrapper([this]{ release(); }, releaseEventName));
            releaseTick.resize((idx+2), 0);
            // const std::string dequeueEventName = csprintf("%s_dequeue[%d]", name(), (idx+1));
            // dequeueEvent.resize((idx+2), EventFunctionWrapper([this]{ dequeue(); }, dequeueEventName));
            isBusy.resize((idx+2), false);
            retryReq.resize((idx+2), false);
            retryResp.resize((idx+2), false);
        }
        if (spm_ports[idx] == nullptr) {
            const std::string portName = csprintf("%s.spm_ports[%d]", name(), idx);
            spm_ports[idx] = new SPMPort(portName, this, idx);
        }
        return *spm_ports[idx];
    }
    return AbstractMemory::getPort(if_name, idx);
}

DrainState
ScratchpadMemory::drain()
{
    if (!packetQueue.empty()) {
        DPRINTF(Drain, "ScratchpadMemory Queue has requests, waiting to drain\n");
        return DrainState::Draining;
    } else {
        return DrainState::Drained;
    }
}

ScratchpadMemory::MemoryPort::MemoryPort(const std::string& _name,
                                     ScratchpadMemory& _memory)
    : SlavePort(_name, &_memory), memory(_memory)
{ }

AddrRangeList
ScratchpadMemory::MemoryPort::getAddrRanges() const
{
    AddrRangeList ranges;
    ranges.push_back(memory.getAddrRange());
    return ranges;
}

Tick
ScratchpadMemory::MemoryPort::recvAtomic(PacketPtr pkt)
{
    return memory.recvAtomic(pkt);
}

Tick
ScratchpadMemory::MemoryPort::recvAtomicBackdoor(
        PacketPtr pkt, MemBackdoorPtr &_backdoor)
{
    return memory.recvAtomicBackdoor(pkt, _backdoor);
}

void
ScratchpadMemory::MemoryPort::recvFunctional(PacketPtr pkt)
{
    memory.recvFunctional(pkt);
}

bool
ScratchpadMemory::MemoryPort::recvTimingReq(PacketPtr pkt)
{
    return memory.recvTimingReq(pkt, id);
}

void
ScratchpadMemory::MemoryPort::recvRespRetry()
{
    memory.recvRespRetry(id);
}

ScratchpadMemory*
ScratchpadMemoryParams::create()
{
    return new ScratchpadMemory(this);
}