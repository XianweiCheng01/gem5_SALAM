#include "hwacc/comm_interface.hh"
#include "base/trace.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "sim/system.hh"

#include <stdio.h>
#include <stdlib.h>
#include <iomanip>

using namespace std;

/***************************************************************************************
 * CommInterface serves as the general system interface for hardware accelerators. It
 * provides a set of memory-mapped registers, as well as master ports for accessing
 * both local busses/SPMs and system memory.
 **************************************************************************************/
CommInterface::CommInterface(Params *p) :
    BasicPioDevice(p, p->pio_size),
    io_addr(p->pio_addr),
    io_size(p->pio_size),
    flag_size(p->flags_size),
    config_size(p->config_size),
    devname(p->devicename),
    gic(p->gic),
    int_num(p->int_num),
    use_premap_data(p->premap_data),
    masterId(p->system->getMasterId(this,name())),
    tickEvent(this),
    cacheLineSize(p->cache_line_size),
    clock_period(p->clock_period),
    endian(p->system->getGuestByteOrder()),
    debugEnabled(p->enable_debug_msgs),
    reset_spm(p->reset_spm) {
    processDelay = 1000 * clock_period;
    FLAG_OFFSET = 0;
    CONFIG_OFFSET = flag_size;
    VAR_OFFSET = CONFIG_OFFSET + config_size;
    processingDone = false;
    computationNeeded = false;
    int_flag = false;

    mmreg = new uint8_t[io_size];
    for(int i = 0; i < io_size; i++) {
        mmreg[i] = 0;
    }
    cu = nullptr;

    if (use_premap_data) {
        for (auto i = 0; i < p->data_bases.size(); i++) {
            data_base_ptrs.push_back(p->data_bases[i]);
        }
    }
}

bool
CommInterface::MemSidePort::recvTimingResp(PacketPtr pkt) {
    owner->recvPacket(pkt);
    return true;
}

void
CommInterface::MemSidePort::recvReqRetry() {
    assert(outstandingPkts.size());

    if (debug()) DPRINTF(CommInterface, "Got a retry...\n");
    while (outstandingPkts.size() && sendTimingReq(outstandingPkts.front())) {
        if (debug()) DPRINTF(CommInterface, "Unblocked, sent blocked packet.\n");
        outstandingPkts.pop();
        // TODO: This should just signal the engine that the packet completed
        // engine should schedule tick as necessary. Need a test case
        if (!owner->tickEvent.scheduled()) {
            owner->schedule(owner->tickEvent, curTick() + owner->processDelay);
            //owner->schedule(owner->tickEvent, owner->nextCycle());
        }
    }
}

void
CommInterface::MemSidePort::sendPacket(PacketPtr pkt) {
    //printf("Test 0 in MemSidePort::sendPacket of src/hwacc/comm_interface.cc before sendTimingReq\n");
    fflush(stdout);
    if (isStalled() || !sendTimingReq(pkt)) {
        if (debug()) DPRINTF(CommInterface, "sendTiming failed in sendPacket(pkt->req->getPaddr()=0x%x)\n", (unsigned int)pkt->req->getPaddr());
        setStalled(pkt);
    }
}

bool
CommInterface::SPMPort::recvTimingResp(PacketPtr pkt) {
    owner->recvPacket(pkt);
    return true;
}

void
CommInterface::SPMPort::recvReqRetry() {
    assert(outstandingPkts.size());

    if (debug()) DPRINTF(CommInterface, "Got a retry...\n");
    while (outstandingPkts.size() && sendTimingReq(outstandingPkts.front())) {
        if (debug()) DPRINTF(CommInterface, "Unblocked, sent blocked packet.\n");
        outstandingPkts.pop();
        // TODO: This should just signal the engine that the packet completed
        // engine should schedule tick as necessary. Need a test case
        if (!owner->tickEvent.scheduled()) {
            owner->schedule(owner->tickEvent, curTick() + owner->processDelay);
            //owner->schedule(owner->tickEvent, owner->nextCycle());
        }
    }
}

void
CommInterface::SPMPort::sendPacket(PacketPtr pkt) {
    //printf("Test 0 in SPMPort::sendPacket of src/hwacc/comm_interface.cc before sendTimingReq\n");
    fflush(stdout);
    if (isStalled() || !sendTimingReq(pkt)) {
        if (debug()) DPRINTF(CommInterface, "sendTiming failed in sendPacket(pkt->req->getPaddr()=0x%x)\n", (unsigned int)pkt->req->getPaddr());
        setStalled(pkt);
    }
}

void
CommInterface::recvPacket(PacketPtr pkt) {
	if (pkt->isRead()) {
        MemoryRequest * readReq = findMemRequest(pkt, true);
        MasterPort * carrier = readReq->getCarrierPort();
        if (MemSidePort * port = dynamic_cast<MemSidePort *>(carrier)) port->readReq = nullptr;
        if (SPMPort * port = dynamic_cast<SPMPort *>(carrier)) port->readReq = nullptr;
        if (debug()) DPRINTF(CommInterface, "Done with a read. addr: 0x%x, size: %d\n", pkt->req->getPaddr(), pkt->getSize());
        pkt->writeData(readReq->buffer + (pkt->req->getPaddr() - readReq->beginAddr));
        if (debug()) DPRINTF(CommInterface, "Read:0x%016lx\n", *(uint64_t *)readReq->buffer);
        for (int i = pkt->req->getPaddr() - readReq->beginAddr;
             i < pkt->req->getPaddr() - readReq->beginAddr + pkt->getSize(); i++)
        {
            readReq->readsDone[i] = true;
        }

        // mark readDone as only the contiguous region
        while (readReq->readDone < readReq->totalLength && readReq->readsDone[readReq->readDone])
        {
            readReq->readDone++;
        }

        if (!readReq->needToRead)
        {
            if (debug()) DPRINTF(CommInterface, "Done reading\n");
            cu->readCommit(readReq);
            clearMemRequest(readReq, true);
            delete readReq;
        } else {
            readQueue.push_front(readReq);
            clearMemRequest(readReq, true); // Clear the request from the in-flight queue
        }
    } else if (pkt->isWrite()) {
        MemoryRequest * writeReq = findMemRequest(pkt, false);
        MasterPort * carrier = writeReq->getCarrierPort();
        if (MemSidePort * port = dynamic_cast<MemSidePort *>(carrier)) port->writeReq = nullptr;
        if (SPMPort * port = dynamic_cast<SPMPort *>(carrier)) port->writeReq = nullptr;
        if (debug()) DPRINTF(CommInterface, "Done with a write. addr: 0x%x, size: %d\n", pkt->req->getPaddr(), pkt->getSize());
        writeReq->writeDone += pkt->getSize();
        if (!(writeReq->needToWrite)) {
            if (debug()) DPRINTF(CommInterface, "Done writing\n");
            cu->writeCommit(writeReq);
            delete[] writeReq->buffer;
            delete[] writeReq->readsDone;
            clearMemRequest(writeReq, false);
            delete writeReq;
        } else {
            writeQueue.push_front(writeReq);
            clearMemRequest(writeReq, false); // Clear the request from the in-flight queue
        }
    } else {
        panic("Something went very wrong!");
    }
    if (!tickEvent.scheduled())
    {
        schedule(tickEvent, curTick() + processDelay);
        //schedule(tickEvent, nextCycle());
    }
    //if (pkt->req) delete pkt->req;
    delete pkt;
}

void
CommInterface::checkMMR() {
    if (!computationNeeded) {
        if (debug()) DPRINTF(CommInterface, "Checking MMR to see if Run bit set\n");
        if (*mmreg & 0x01) {
            *mmreg &= 0xfe;
            *mmreg |= 0x02;
            computationNeeded = true;
            cu->initialize();
        }

        if (processingDone && !tickEvent.scheduled()) {
            processingDone = false;
            schedule(tickEvent, curTick() + processDelay);
            //schedule(tickEvent, nextCycle());
        }
    }
}

bool
CommInterface::inStreamRange(Addr add) {
    for (auto port : streamPorts) {
        AddrRangeList adl = port->getAddrRanges();
        for (auto address : adl) {
            if (address.contains(add)) {
                return true;
            }
        }
    }
    return false;
}

bool
CommInterface::inSPMRange(Addr add) {
    for (auto port : spmPorts) {
        AddrRangeList adl = port->getAddrRanges();
        for (auto address : adl) {
            if (address.contains(add)) {
                return true;
            }
        }
    }
    return false;
}

bool
CommInterface::inLocalRange(Addr add) {
    for (auto port : localPorts) {
        AddrRangeList adl = port->getAddrRanges();
        for (auto address : adl) {
            if (address.contains(add)) {
                return true;
            }
        }
    }
    return false;
}

bool
CommInterface::inGlobalRange(Addr add) {
    for (auto port : globalPorts) {
        AddrRangeList adl = port->getAddrRanges();
        for (auto address : adl) {
            if (address.contains(add)) {
                return true;
            }
        }
    }
    return false;
}

CommInterface::MemSidePort *
CommInterface::getValidLocalPort(Addr add, bool read) {
    for (auto port : localPorts) {
        AddrRangeList adl = port->getAddrRanges();
        for (auto address : adl) {
            if (address.contains(add) && !(port->isStalled())) {
                if ((read && !(port->readReq)) || (!read && !(port->writeReq))) {
                    return port;
                }
            }
        }
    }
    return nullptr;
}
CommInterface::MemSidePort *
CommInterface::getValidGlobalPort(Addr add, bool read) {
    for (auto port : globalPorts) {
        AddrRangeList adl = port->getAddrRanges();
        for (auto address : adl) {
            if (address.contains(add) && !(port->isStalled())) {
                if ((read && !(port->readReq)) || (!read && !(port->writeReq))) {
                    return port;
                }
            }
        }
    }
    return nullptr;
}
CommInterface::MemSidePort *
CommInterface::getValidStreamPort(Addr add, size_t len, bool read) {
    for (auto port : streamPorts) {
        AddrRangeList adl = port->getAddrRanges();
        for (auto address : adl) {
            if (address.contains(add) && !(port->isStalled())) {
                if (((read && !(port->readReq)) || (!read && !(port->writeReq))) && port->streamValid(len, read)) {
                    return port;
                }
            }
        }
    }
    return nullptr;
}

CommInterface::SPMPort *
CommInterface::getValidSPMPort(Addr add, size_t len, bool read) {
    for (auto port : spmPorts) {
        AddrRangeList adl = port->getAddrRanges();
        for (auto address : adl) {
            if (address.contains(add) && !(port->isStalled())) {
                if (((read && !(port->readReq)) || (!read && !(port->writeReq)))) {
                    if (port->canAccess(add, len, read))
                        return port;
                }
            }
        }
    }
    return nullptr;
}

void
CommInterface::processMemoryRequests() {
    if (!allPortsStalled()) {
        //printf("Test 0 in processMemoryRequests of src/hwacc/comm_interface.cc\n");
        fflush(stdout);
        if (debug()) DPRINTF(CommInterface, "Checking read requests. %d requests in queue.\n", readQueue.size());
        for (auto it=readQueue.begin(); it!=readQueue.end(); ) {
            Addr address = (*it)->currentReadAddr;
            if (debug()) DPRINTF(CommInterfaceQueues, "Request Address: %lx\n", address);
            MasterPort * mport;
            if (inStreamRange(address)) {
                mport = getValidStreamPort(address, (*it)->length, true);
            } else if (inSPMRange(address)) {
                mport = getValidSPMPort(address, (*it)->length, true);
            } else if (inLocalRange(address)) {
                mport = getValidLocalPort(address, true);
            } else if (inGlobalRange(address)) {
                mport = getValidGlobalPort(address, true);
            } else {
                panic("Address %lx is not reachable by any ports\n", address);
            }
            if (SPMPort * port = dynamic_cast<SPMPort *>(mport)) {
                if (debug()) DPRINTF(CommInterfaceQueues, "Found available memory port\n");
                port->readReq = (*it);
                port->readReq->setCarrierPort(port);
                it = readQueue.erase(it);
                if (port->readReq && port->readReq->needToRead) {
                    if (debug()) DPRINTF(CommInterfaceQueues, "Trying read on available memory port\n");
                    tryRead(port);
                    accRdQ.push_back(port->readReq);
                    // if (!port->readReq->needToRead)
                    //     port->readReq = NULL;
                }
            } else if (MemSidePort * port = dynamic_cast<MemSidePort *>(mport)) {
                if (debug()) DPRINTF(CommInterfaceQueues, "Found available memory port\n");
                port->readReq = (*it);
                port->readReq->setCarrierPort(port);
                it = readQueue.erase(it);
                if (port->readReq && port->readReq->needToRead) {
                    if (debug()) DPRINTF(CommInterfaceQueues, "Trying read on available memory port\n");
                    tryRead(port);
                    accRdQ.push_back(port->readReq);
                    // if (!port->readReq->needToRead)
                    //     port->readReq = NULL;
                }
            } else {
                if (debug()) DPRINTF(CommInterfaceQueues, "Found no ports able to read %d bytes from %lx\n", (*it)->length, address);
                ++it;
            }
        }
        if (debug()) DPRINTF(CommInterface, "Checking write requests. %d requests in queue.\n", writeQueue.size());
        for (auto it=writeQueue.begin(); it!=writeQueue.end(); ) {
            Addr address = (*it)->currentWriteAddr;
            if (debug()) DPRINTF(CommInterfaceQueues, "Request Address: %lx\n", address);
            MasterPort * mport;
            if (inStreamRange(address)) {
                mport = getValidStreamPort(address, (*it)->length, false);
            } else if (inSPMRange(address)) {
                mport = getValidSPMPort(address, (*it)->length, false);
            } else if (inLocalRange(address)) {
                mport = getValidLocalPort(address, false);
            } else if (inGlobalRange(address)) {
                mport = getValidGlobalPort(address, false);
            } else {
                panic("Address %lx is not reachable by any ports\n", address);
            }
            if (SPMPort * port = dynamic_cast<SPMPort*>(mport)) {
                if (debug()) DPRINTF(CommInterfaceQueues, "Found available memory port\n");
                port->writeReq = (*it);
                port->writeReq->setCarrierPort(port);
                it = writeQueue.erase(it);
                if (port->writeReq && port->writeReq->needToWrite) {
                    if (debug()) DPRINTF(CommInterfaceQueues, "Trying write on available memory port\n");
                    tryWrite(port);
                    accWrQ.push_back(port->writeReq);
                    // if (!port->writeReq->needToWrite)
                    //     port->writeReq = NULL;
                }
            } else if (MemSidePort * port = dynamic_cast<MemSidePort*>(mport)) {
                if (debug()) DPRINTF(CommInterfaceQueues, "Found available memory port\n");
                port->writeReq = (*it);
                port->writeReq->setCarrierPort(port);
                it = writeQueue.erase(it);
                if (port->writeReq && port->writeReq->needToWrite) {
                    if (debug()) DPRINTF(CommInterfaceQueues, "Trying write on available memory port\n");
                    tryWrite(port);
                    accWrQ.push_back(port->writeReq);
                    // if (!port->writeReq->needToWrite)
                    //     port->writeReq = NULL;
                }
            } else {
                if (debug()) DPRINTF(CommInterfaceQueues, "Found no ports able to write %d bytes to %lx\n", (*it)->length, address);
                ++it;
            }
        }
        //printf("Test 1 in processMemoryRequests of src/hwacc/comm_interface.cc\n");
        fflush(stdout);
    } else {
        if (debug()) DPRINTF(CommInterface, "All ports are stalled\n");
    }
    requestsInQueues = readQueue.size() + writeQueue.size();
    if (!tickEvent.scheduled() && requestsInQueues>0) {
        schedule(tickEvent, curTick() + processDelay);
        //schedule(tickEvent, nextCycle());
    }
}

void
CommInterface::tick() {
    if (debug()) DPRINTF(CommInterface, "Tick!\n");
    //printf("Test 0 in tick of comm_interface.cc\n");
    //fflush(stdout);
    checkMMR();
    requestsInQueues = readQueue.size() + writeQueue.size();
    if (requestsInQueues > 0)
        processMemoryRequests();
    //printf("Test 1 in tick of comm_interface.cc\n");
    //fflush(stdout);
}

void
CommInterface::tryRead(MemSidePort * port) {
    MemoryRequest * readReq = port->readReq;
    Request::Flags flags;
    if (readReq->readLeft <= 0) {
        if (debug()) DPRINTF(CommInterface, "Something went wrong. Shouldn't try to read if there aren't reads left\n");
        return;
    }
    int size;
    if (readReq->currentReadAddr % cacheLineSize) {
        size = cacheLineSize - (readReq->currentReadAddr % cacheLineSize);
        if (debug()) DPRINTF(CommInterface, "Aligning\n");
    } else {
        size = cacheLineSize;
    }
    size = readReq->readLeft > (size - 1) ? size : readReq->readLeft;
    RequestPtr req = make_shared<Request>(readReq->currentReadAddr, size, flags, masterId);
    if (debug()) DPRINTF(CommInterface, "Trying to read addr: 0x%016x, %d bytes through port: %s\n",
        req->getPaddr(), size, port->name());

    PacketPtr pkt = new Packet(req, MemCmd::ReadReq);
    pkt->allocate();
    readReq->pkt = pkt;
    port->sendPacket(pkt);

    readReq->currentReadAddr += size;

    readReq->readLeft -= size;

    if (!(readReq->readLeft > 0)) {
        readReq->needToRead = false;
        if (!tickEvent.scheduled()) {
            schedule(tickEvent, curTick() + processDelay);
            //schedule(tickEvent, nextCycle());
        }
    } else {
        if (!port->isStalled() && !tickEvent.scheduled())
        {
            schedule(tickEvent, curTick() + processDelay);
            //schedule(tickEvent, nextCycle());
        }
    }
}

void
CommInterface::tryWrite(MemSidePort * port) {
    MemoryRequest * writeReq = port->writeReq;
    if (writeReq->writeLeft <= 0) {
        if (debug()) DPRINTF(CommInterface, "Something went wrong. Shouldn't try to write if there aren't writes left\n");
        return;
    }

    int size;
    if (writeReq->currentWriteAddr % cacheLineSize) {
        size = cacheLineSize - (writeReq->currentWriteAddr % cacheLineSize);
        if (debug()) DPRINTF(CommInterface, "Aligning\n");
    } else {
        size = cacheLineSize;
    }
    size = writeReq->writeLeft > size - 1 ? size : writeReq->writeLeft;

    Request::Flags flags;
    uint8_t *data = new uint8_t[size];
    std::memcpy(data, &(writeReq->buffer[writeReq->totalLength-writeReq->writeLeft]), size);
    RequestPtr req = make_shared<Request>(writeReq->currentWriteAddr, size, flags, masterId);
    req->setExtraData((uint64_t)data);


    if (debug()) DPRINTF(CommInterface, "totalLength: %d, writeLeft: %d\n", writeReq->totalLength, writeReq->writeLeft);
    if (debug()) DPRINTF(CommInterface, "Trying to write to addr: 0x%016x, %d bytes, data 0x%08x through port: %s\n",
        writeReq->currentWriteAddr, size,
        *((uint64_t*)(&(writeReq->buffer[writeReq->totalLength-writeReq->writeLeft]))),
        port->name());

    PacketPtr pkt = new Packet(req, MemCmd::WriteReq);
    uint8_t *pkt_data = (uint8_t *)req->getExtraData();
    pkt->dataDynamic(pkt_data);
    writeReq->pkt = pkt;
    port->sendPacket(pkt);

    writeReq->currentWriteAddr += size;
    writeReq->writeLeft -= size;

    if (!(writeReq->writeLeft > 0)) {
        writeReq->needToWrite = false;
        if (!tickEvent.scheduled()) {
            schedule(tickEvent, curTick() + processDelay);
            //schedule(tickEvent, nextCycle());
        }
    } else if (!port->isStalled() && !tickEvent.scheduled()) {
            schedule(tickEvent, curTick() + processDelay);
            //schedule(tickEvent, nextCycle());
    }
}

void
CommInterface::tryRead(SPMPort * port) {
    MemoryRequest * readReq = port->readReq;
    Request::Flags flags;
    if (readReq->readLeft <= 0) {
        if (debug()) DPRINTF(CommInterface, "Something went wrong. Shouldn't try to read if there aren't reads left\n");
        return;
    }
    int size;
    if (readReq->currentReadAddr % cacheLineSize) {
        size = cacheLineSize - (readReq->currentReadAddr % cacheLineSize);
        if (debug()) DPRINTF(CommInterface, "Aligning\n");
    } else {
        size = cacheLineSize;
    }
    size = readReq->readLeft > (size - 1) ? size : readReq->readLeft;
    RequestPtr req = make_shared<Request>(readReq->currentReadAddr, size, flags, masterId);
    if (debug()) DPRINTF(CommInterface, "Trying to read addr: 0x%016x, %d bytes through port: %s\n",
        req->getPaddr(), size, port->name());

    PacketPtr pkt = new Packet(req, MemCmd::ReadReq);
    pkt->allocate();
    readReq->pkt = pkt;
    port->sendPacket(pkt);

    readReq->currentReadAddr += size;

    readReq->readLeft -= size;

    if (!(readReq->readLeft > 0)) {
        readReq->needToRead = false;
        if (!tickEvent.scheduled()) {
            schedule(tickEvent, curTick() + processDelay);
            //schedule(tickEvent, nextCycle());
        }
    } else {
        if (!port->isStalled() && !tickEvent.scheduled())
        {
            schedule(tickEvent, curTick() + processDelay);
            //schedule(tickEvent, nextCycle());
        }
    }
}

void
CommInterface::tryWrite(SPMPort * port) {
    MemoryRequest * writeReq = port->writeReq;
    if (writeReq->writeLeft <= 0) {
        if (debug()) DPRINTF(CommInterface, "Something went wrong. Shouldn't try to write if there aren't writes left\n");
        return;
    }

    int size;
    if (writeReq->currentWriteAddr % cacheLineSize) {
        size = cacheLineSize - (writeReq->currentWriteAddr % cacheLineSize);
        if (debug()) DPRINTF(CommInterface, "Aligning\n");
    } else {
        size = cacheLineSize;
    }
    size = writeReq->writeLeft > size - 1 ? size : writeReq->writeLeft;

    Request::Flags flags;
    uint8_t *data = new uint8_t[size];
    std::memcpy(data, &(writeReq->buffer[writeReq->totalLength-writeReq->writeLeft]), size);
    RequestPtr req = make_shared<Request>(writeReq->currentWriteAddr, size, flags, masterId);
    req->setExtraData((uint64_t)data);


    if (debug()) DPRINTF(CommInterface, "totalLength: %d, writeLeft: %d\n", writeReq->totalLength, writeReq->writeLeft);
    if (debug()) DPRINTF(CommInterface, "Trying to write to addr: 0x%016x, %d bytes, data 0x%08x through port: %s\n",
        writeReq->currentWriteAddr, size,
        *((uint64_t*)(&(writeReq->buffer[writeReq->totalLength-writeReq->writeLeft]))),
        port->name());

    PacketPtr pkt = new Packet(req, MemCmd::WriteReq);
    uint8_t *pkt_data = (uint8_t *)req->getExtraData();
    pkt->dataDynamic(pkt_data);
    writeReq->pkt = pkt;
    port->sendPacket(pkt);

    writeReq->currentWriteAddr += size;
    writeReq->writeLeft -= size;

    if (!(writeReq->writeLeft > 0)) {
        writeReq->needToWrite = false;
        if (!tickEvent.scheduled()) {
            schedule(tickEvent, curTick() + processDelay);
            //schedule(tickEvent, nextCycle());
        }
    } else if (!port->isStalled() && !tickEvent.scheduled()) {
            schedule(tickEvent, curTick() + processDelay);
            //schedule(tickEvent, nextCycle());
    }
}

void
CommInterface::enqueueRead(MemoryRequest * req) {
    if (debug()) DPRINTF(CommInterface, "Read from 0x%lx of Size:%d Bytes Enqueued:\n", req->address, req->length);
    readQueue.push_back(req);
    if (debug()) DPRINTF(CommInterfaceQueues, "Current Queue:\n");
    for (auto it=readQueue.begin(); it!=readQueue.end(); ++it) {
        if (debug()) DPRINTF(CommInterfaceQueues, "Read Request: %lx\n", (*it)->address);
    }
    if (!tickEvent.scheduled()) {
        schedule(tickEvent, curTick() + processDelay);
        //schedule(tickEvent, nextCycle());
    }
}

void
CommInterface::enqueueWrite(MemoryRequest * req) {
    if (debug()) DPRINTF(CommInterface, "Write to 0x%lx of size:%d bytes enqueued\n", req->address, req->length);
    writeQueue.push_back(req);
    if (debug()) DPRINTF(CommInterfaceQueues, "Current Queue:\n");
    for (auto it=writeQueue.begin(); it!=writeQueue.end(); ++it) {
        if (debug()) DPRINTF(CommInterfaceQueues, "Write Request: %lx\n", (*it)->address);
    }
    if (!tickEvent.scheduled()) {
        schedule(tickEvent, curTick() + processDelay);
        //schedule(tickEvent, nextCycle());
    }
}

void
CommInterface::finish() {
    *mmreg &= 0xfc;
    *mmreg |= 0x04;
    computationNeeded = false;
    if (int_num>0) {
        printf("Test 0 in finish of comm_interface.cc with %d\n", int_num);
        int_flag = true;
        gic->sendInt(int_num);
        printf("Test 1 in finish of comm_interface.cc with %d\n", int_num);
    }
    if (reset_spm) {
        for (auto port : spmPorts) {
            port->setReadyStatus(false);
        }
    }
}

Tick
CommInterface::read(PacketPtr pkt) {
    if (debug()) DPRINTF(DeviceMMR, "The address range associated with this ACC was read!\n");

    Addr offset = pkt->req->getPaddr() - io_addr;

    uint64_t data;

    data = *(uint64_t *)(mmreg+offset);

    switch(pkt->getSize()) {
      case 1:
        pkt->set<uint8_t>(data, endian);
        break;
      case 2:
        pkt->set<uint16_t>(data, endian);
        break;
      case 4:
        pkt->set<uint32_t>(data, endian);
        break;
      case 8:
        pkt->set<uint64_t>(data, endian);
        break;
      default:
        panic("Read size too big?\n");
        break;
    }

    pkt->makeAtomicResponse();
    return pioDelay;
}

Tick
CommInterface::write(PacketPtr pkt) {
    if (debug()) DPRINTF(DeviceMMR,
        "The address range associated with this ACC was written to!\n");

    if (debug()) DPRINTF(DeviceMMR, "Packet addr 0x%lx\n", pkt->req->getPaddr());
    if (debug()) DPRINTF(DeviceMMR, "IO addr 0x%lx\n", io_addr);
    if (debug()) DPRINTF(DeviceMMR, "Diff addr 0x%lx\n", pkt->req->getPaddr() - io_addr);
    if (debug()) DPRINTF(DeviceMMR, "Packet val (LE) %d\n", pkt->getLE<uint8_t>());
    if (debug()) DPRINTF(DeviceMMR, "Packet val (BE) %d\n", pkt->getBE<uint8_t>());
    if (debug()) DPRINTF(DeviceMMR, "Packet val %d\n", pkt->get<uint8_t>(endian));
    pkt->writeData(mmreg + (pkt->req->getPaddr() - io_addr));

    std::stringstream mm;
    for (int i = io_size-1; i >= 0; i--) {
        if ((i >= flag_size+config_size) && ((i-flag_size-config_size)%8 == 0))
            mm << std::setfill('0') << std::setw(2) << std::hex << (uint32_t)mmreg[i] << "|";
        else if (i == flag_size+config_size)
            mm << "|" << std::setfill('0') << std::setw(2) << std::hex << (uint32_t)mmreg[i];
        else if (i == flag_size)
            mm << "|" << std::setfill('0') << std::setw(2) << std::hex << (uint32_t)mmreg[i];
        else
            mm << std::setfill('0') << std::setw(2) << std::hex << (uint32_t)mmreg[i];
    }
    std::string mmr = mm.str();
    if (debug()) DPRINTF(DeviceMMR, "MMReg value: %s\n", mmr);

    pkt->makeAtomicResponse();

    if (((*mmreg & 0x04) == 0x00) && int_flag) {
        if (int_num > 0)
            gic->clearInt(int_num);
        int_flag = false;
    }
    if (!tickEvent.scheduled()) {
        schedule(tickEvent, nextCycle());
    }

    return pioDelay;
}

uint64_t
CommInterface::getGlobalVar(unsigned offset, unsigned size) {
    if (use_premap_data) {
        return data_base_ptrs.at(offset/8);
    } else {
        uint64_t value;
        switch (size) {
            case 1:
                value = *(uint64_t *)(uint8_t *)(mmreg + VAR_OFFSET + offset);
                break;
            case 2:
                value = *(uint64_t *)(uint16_t *)(mmreg + VAR_OFFSET + offset);
                break;
            case 4:
                value = *(uint64_t *)(uint32_t *)(mmreg + VAR_OFFSET + offset);
                break;
            case 8:
                value = *(uint64_t *)(mmreg + VAR_OFFSET + offset);
                break;
            default:
                panic("Data of size: %d is not supported as a global variable!");
        }
        return value;
    }
}

CommInterface *
CommInterfaceParams::create() {
    return new CommInterface(this);
}

Port&
CommInterface::getPort(const std::string& if_name, PortID idx) {
    if (if_name == "local") {
        if (idx >= localPorts.size()) {
            localPorts.resize((idx+1), nullptr);
        }
        if (localPorts[idx] == nullptr) {
            const std::string portName = csprintf("%s.local[%d]", name(), idx);
            localPorts[idx] = new MemSidePort(portName, this, idx);
        }
        return *localPorts[idx];
    } else if (if_name == "acp") {
        if (idx >= globalPorts.size()) {
            globalPorts.resize((idx+1), nullptr);
        }
        if (globalPorts[idx] == nullptr) {
            const std::string portName = csprintf("%s.acp[%d]", name(), idx);
            globalPorts[idx] = new MemSidePort(portName, this, idx);
        }
        return *globalPorts[idx];
    } else if (if_name == "stream") {
        if (idx >= streamPorts.size()) {
            streamPorts.resize((idx+1), nullptr);
        }
        if (streamPorts[idx] == nullptr) {
            const std::string portName = csprintf("%s.stream[%d]", name(), idx);
            streamPorts[idx] = new MemSidePort(portName, this, idx);
        }
        return *streamPorts[idx];
    } else if (if_name == "spm") {
        if (idx >= spmPorts.size()) {
            spmPorts.resize((idx+1), nullptr);
        }
        if (spmPorts[idx] == nullptr) {
            const std::string portName = csprintf("%s.spm[%d]", name(), idx);
            spmPorts[idx] = new SPMPort(portName, this, idx);
        }
        return *spmPorts[idx];
    } else if (if_name == "pio") {
        return pioPort;
    } else {
        return ClockedObject::getPort(if_name, idx);
    }
}

MemoryRequest *
CommInterface::findMemRequest(PacketPtr pkt, bool isRead) {
    if (isRead) {
        for (auto it=accRdQ.begin(); it!=accRdQ.end(); ++it) {
            if ((*it)->pkt == pkt) {
                return (*it);
            }
        }
    } else {
        for (auto it=accWrQ.begin(); it!=accWrQ.end(); ++it) {
            if ((*it)->pkt == pkt) {
                return (*it);
            }
        }
    }
    panic("Could not find memory request in request queues");
    return NULL;
}

void
CommInterface::clearMemRequest(MemoryRequest * req, bool isRead) {
    if (isRead) {
        for (auto it=accRdQ.begin(); it!=accRdQ.end(); ++it) {
            if ((*it) == req) {
                it=accRdQ.erase(it);
                break;
            }
        }
    } else {
        for (auto it=accWrQ.begin(); it!=accWrQ.end(); ++it) {
            if ((*it) == req) {
                it=accWrQ.erase(it);
                break;
            }
        }
    }
}

void
CommInterface::startup() {
    // AddrRangeList dramSideRanges = dramPort->getAddrRanges();
    // AddrRangeList spmSideRanges = spmPort->getAddrRanges();
    // std::cout << "DRAM Port Ranges:\n";
    // for (auto it = dramSideRanges.begin(); it != dramSideRanges.end(); ++it) {
    //     std::cout << it->to_string() << std::endl;
    // }
    // std::cout << "SPM Port Ranges:\n";
    // for (auto it = spmSideRanges.begin(); it != spmSideRanges.end(); ++it) {
    //     std::cout << it->to_string() << std::endl;
    // }
}
