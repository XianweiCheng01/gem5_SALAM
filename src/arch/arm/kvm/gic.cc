/*
 * Copyright (c) 2015-2017 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
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
 *
 * Authors: Andreas Sandberg
 *          Curtis Dunham
 */

#include "arch/arm/kvm/gic.hh"

#include <linux/kvm.h>

#include "arch/arm/kvm/base_cpu.hh"
#include "debug/GIC.hh"
#include "debug/Interrupt.hh"
#include "params/MuxingKvmGic.hh"

KvmKernelGicV2::KvmKernelGicV2(KvmVM &_vm, Addr cpu_addr, Addr dist_addr,
                               unsigned it_lines)
    : cpuRange(RangeSize(cpu_addr, KVM_VGIC_V2_CPU_SIZE)),
      distRange(RangeSize(dist_addr, KVM_VGIC_V2_DIST_SIZE)),
      vm(_vm),
      kdev(vm.createDevice(KVM_DEV_TYPE_ARM_VGIC_V2))
{
    // Tell the VM that we will emulate the GIC in the kernel. This
    // disables IRQ and FIQ handling in the KVM CPU model.
    vm.enableKernelIRQChip();

    kdev.setAttr<uint64_t>(
        KVM_DEV_ARM_VGIC_GRP_ADDR, KVM_VGIC_V2_ADDR_TYPE_DIST, dist_addr);
    kdev.setAttr<uint64_t>(
        KVM_DEV_ARM_VGIC_GRP_ADDR, KVM_VGIC_V2_ADDR_TYPE_CPU, cpu_addr);

    kdev.setAttr<uint32_t>(KVM_DEV_ARM_VGIC_GRP_NR_IRQS, 0, it_lines);
}

KvmKernelGicV2::~KvmKernelGicV2()
{
}

void
KvmKernelGicV2::setSPI(unsigned spi)
{
    setIntState(KVM_ARM_IRQ_TYPE_SPI, 0, spi, true);
}

void
KvmKernelGicV2::clearSPI(unsigned spi)
{
    setIntState(KVM_ARM_IRQ_TYPE_SPI, 0, spi, false);
}

void
KvmKernelGicV2::setPPI(unsigned vcpu, unsigned ppi)
{
    setIntState(KVM_ARM_IRQ_TYPE_PPI, vcpu, ppi, true);
}

void
KvmKernelGicV2::clearPPI(unsigned vcpu, unsigned ppi)
{
    setIntState(KVM_ARM_IRQ_TYPE_PPI, vcpu, ppi, false);
}

void
KvmKernelGicV2::setIntState(unsigned type, unsigned vcpu, unsigned irq,
                            bool high)
{
    assert(type <= KVM_ARM_IRQ_TYPE_MASK);
    assert(vcpu <= KVM_ARM_IRQ_VCPU_MASK);
    assert(irq <= KVM_ARM_IRQ_NUM_MASK);
    const uint32_t line(
        (type << KVM_ARM_IRQ_TYPE_SHIFT) |
        (vcpu << KVM_ARM_IRQ_VCPU_SHIFT) |
        (irq << KVM_ARM_IRQ_NUM_SHIFT));

    vm.setIRQLine(line, high);
}

uint32_t
KvmKernelGicV2::getGicReg(unsigned group, unsigned vcpu, unsigned offset)
{
    uint64_t reg;

    assert(vcpu <= KVM_ARM_IRQ_VCPU_MASK);
    const uint64_t attr(
        ((uint64_t)vcpu << KVM_DEV_ARM_VGIC_CPUID_SHIFT) |
        (offset << KVM_DEV_ARM_VGIC_OFFSET_SHIFT));

    kdev.getAttrPtr(group, attr, &reg);
    return (uint32_t) reg;
}

void
KvmKernelGicV2::setGicReg(unsigned group, unsigned vcpu, unsigned offset,
                          unsigned value)
{
    uint64_t reg = value;

    assert(vcpu <= KVM_ARM_IRQ_VCPU_MASK);
    const uint64_t attr(
        ((uint64_t)vcpu << KVM_DEV_ARM_VGIC_CPUID_SHIFT) |
        (offset << KVM_DEV_ARM_VGIC_OFFSET_SHIFT));

    kdev.setAttrPtr(group, attr, &reg);
}

uint32_t
KvmKernelGicV2::readDistributor(ContextID ctx, Addr daddr)
{
    auto vcpu = vm.contextIdToVCpuId(ctx);
    return getGicReg(KVM_DEV_ARM_VGIC_GRP_DIST_REGS, vcpu, daddr);
}

uint32_t
KvmKernelGicV2::readCpu(ContextID ctx, Addr daddr)
{
    auto vcpu = vm.contextIdToVCpuId(ctx);
    return getGicReg(KVM_DEV_ARM_VGIC_GRP_CPU_REGS, vcpu, daddr);
}

void
KvmKernelGicV2::writeDistributor(ContextID ctx, Addr daddr, uint32_t data)
{
    auto vcpu = vm.contextIdToVCpuId(ctx);
    setGicReg(KVM_DEV_ARM_VGIC_GRP_DIST_REGS, vcpu, daddr, data);
}

void
KvmKernelGicV2::writeCpu(ContextID ctx, Addr daddr, uint32_t data)
{
    auto vcpu = vm.contextIdToVCpuId(ctx);
    setGicReg(KVM_DEV_ARM_VGIC_GRP_CPU_REGS, vcpu, daddr, data);
}



MuxingKvmGic::MuxingKvmGic(const MuxingKvmGicParams *p)
    : GicV2(p),
      system(*p->system),
      kernelGic(nullptr),
      usingKvm(false)
{
    if (auto vm = system.getKvmVM()) {
        kernelGic = new KvmKernelGicV2(*vm, p->cpu_addr, p->dist_addr,
                                       p->it_lines);
    }
}

MuxingKvmGic::~MuxingKvmGic()
{
}

void
MuxingKvmGic::startup()
{
    GicV2::startup();
    usingKvm = (kernelGic != nullptr) && system.validKvmEnvironment();
    if (usingKvm)
        fromGicV2ToKvm();
}

DrainState
MuxingKvmGic::drain()
{
    if (usingKvm)
        fromKvmToGicV2();
    return GicV2::drain();
}

void
MuxingKvmGic::drainResume()
{
    GicV2::drainResume();
    bool use_kvm = (kernelGic != nullptr) && system.validKvmEnvironment();
    if (use_kvm != usingKvm) {
        // Should only occur due to CPU switches
        if (use_kvm) // from simulation to KVM emulation
            fromGicV2ToKvm();
        // otherwise, drain() already sync'd the state back to the GicV2

        usingKvm = use_kvm;
    }
}

Tick
MuxingKvmGic::read(PacketPtr pkt)
{
    if (!usingKvm)
        return GicV2::read(pkt);

    panic("MuxingKvmGic: PIO from gem5 is currently unsupported\n");
}

Tick
MuxingKvmGic::write(PacketPtr pkt)
{
    //printf("Test 0 in write of gic.cc before GicV2::write(pkt)\n");
    if (!usingKvm)
        return GicV2::write(pkt);

    panic("MuxingKvmGic: PIO from gem5 is currently unsupported\n");
}

void
MuxingKvmGic::sendInt(uint32_t num)
{
    if (!usingKvm)
        return GicV2::sendInt(num);

    DPRINTF(Interrupt, "Set SPI %d\n", num);
    kernelGic->setSPI(num);
}

void
MuxingKvmGic::clearInt(uint32_t num)
{
    if (!usingKvm)
        return GicV2::clearInt(num);

    DPRINTF(Interrupt, "Clear SPI %d\n", num);
    kernelGic->clearSPI(num);
}

void
MuxingKvmGic::sendPPInt(uint32_t num, uint32_t cpu)
{
    if (!usingKvm)
        return GicV2::sendPPInt(num, cpu);
    DPRINTF(Interrupt, "Set PPI %d:%d\n", cpu, num);
    kernelGic->setPPI(cpu, num);
}

void
MuxingKvmGic::clearPPInt(uint32_t num, uint32_t cpu)
{
    if (!usingKvm)
        return GicV2::clearPPInt(num, cpu);

    DPRINTF(Interrupt, "Clear PPI %d:%d\n", cpu, num);
    kernelGic->clearPPI(cpu, num);
}

void
MuxingKvmGic::updateIntState(int hint)
{
    // During Kvm->GicV2 state transfer, writes to the GicV2 will call
    // updateIntState() which can post an interrupt.  Since we're only
    // using the GicV2 model for holding state in this circumstance, we
    // short-circuit this behavior, as the GicV2 is not actually active.
    if (!usingKvm)
        return GicV2::updateIntState(hint);
}

void
MuxingKvmGic::copyDistRegister(BaseGicRegisters* from, BaseGicRegisters* to,
                               ContextID ctx, Addr daddr)
{
    auto val = from->readDistributor(ctx, daddr);
    DPRINTF(GIC, "copy dist 0x%x 0x%08x\n", daddr, val);
    to->writeDistributor(ctx, daddr, val);
}

void
MuxingKvmGic::copyCpuRegister(BaseGicRegisters* from, BaseGicRegisters* to,
                               ContextID ctx, Addr daddr)
{
    auto val = from->readCpu(ctx, daddr);
    DPRINTF(GIC, "copy cpu  0x%x 0x%08x\n", daddr, val);
    to->writeCpu(ctx, daddr, val);
}

void
MuxingKvmGic::copyBankedDistRange(BaseGicRegisters* from, BaseGicRegisters* to,
                                  Addr daddr, size_t size)
{
    for (int ctx = 0; ctx < system.numContexts(); ++ctx)
        for (auto a = daddr; a < daddr + size; a += 4)
            copyDistRegister(from, to, ctx, a);
}

void
MuxingKvmGic::clearBankedDistRange(BaseGicRegisters* to,
                                   Addr daddr, size_t size)
{
    for (int ctx = 0; ctx < system.numContexts(); ++ctx)
        for (auto a = daddr; a < daddr + size; a += 4)
            to->writeDistributor(ctx, a, 0xFFFFFFFF);
}

void
MuxingKvmGic::copyDistRange(BaseGicRegisters* from, BaseGicRegisters* to,
                            Addr daddr, size_t size)
{
    for (auto a = daddr; a < daddr + size; a += 4)
        copyDistRegister(from, to, 0, a);
}

void
MuxingKvmGic::clearDistRange(BaseGicRegisters* to,
                             Addr daddr, size_t size)
{
    for (auto a = daddr; a < daddr + size; a += 4)
        to->writeDistributor(0, a, 0xFFFFFFFF);
}

void
MuxingKvmGic::copyGicState(BaseGicRegisters* from, BaseGicRegisters* to)
{
    Addr set, clear;
    size_t size;

    /// CPU state (GICC_*)
    // Copy CPU Interface Control Register (CTLR),
    //      Interrupt Priority Mask Register (PMR), and
    //      Binary Point Register (BPR)
    for (int ctx = 0; ctx < system.numContexts(); ++ctx) {
        copyCpuRegister(from, to, ctx, GICC_CTLR);
        copyCpuRegister(from, to, ctx, GICC_PMR);
        copyCpuRegister(from, to, ctx, GICC_BPR);
    }


    /// Distributor state (GICD_*)
    // Copy Distributor Control Register (CTLR)
    copyDistRegister(from, to, 0, GICD_CTLR);

    // Copy interrupt-enabled statuses (I[CS]ENABLERn; R0 is per-CPU banked)
    set   = GicV2::GICD_ISENABLER.start();
    clear = GicV2::GICD_ICENABLER.start();
    size  = GicV2::itLines / 8;
    clearBankedDistRange(to, clear, 4);
    copyBankedDistRange(from, to, set, 4);

    set += 4, clear += 4, size -= 4;
    clearDistRange(to, clear, size);
    copyDistRange(from, to, set, size);

    // Copy pending interrupts (I[CS]PENDRn; R0 is per-CPU banked)
    set   = GicV2::GICD_ISPENDR.start();
    clear = GicV2::GICD_ICPENDR.start();
    size  = GicV2::itLines / 8;
    clearBankedDistRange(to, clear, 4);
    copyBankedDistRange(from, to, set, 4);

    set += 4, clear += 4, size -= 4;
    clearDistRange(to, clear, size);
    copyDistRange(from, to, set, size);

    // Copy active interrupts (I[CS]ACTIVERn; R0 is per-CPU banked)
    set   = GicV2::GICD_ISACTIVER.start();
    clear = GicV2::GICD_ICACTIVER.start();
    size  = GicV2::itLines / 8;
    clearBankedDistRange(to, clear, 4);
    copyBankedDistRange(from, to, set, 4);

    set += 4, clear += 4, size -= 4;
    clearDistRange(to, clear, size);
    copyDistRange(from, to, set, size);

    // Copy interrupt priorities (IPRIORITYRn; R0-7 are per-CPU banked)
    set   = GicV2::GICD_IPRIORITYR.start();
    copyBankedDistRange(from, to, set, 32);

    set += 32;
    size = GicV2::itLines - 32;
    copyDistRange(from, to, set, size);

    // Copy interrupt processor target regs (ITARGETRn; R0-7 are read-only)
    set = GicV2::GICD_ITARGETSR.start() + 32;
    size = GicV2::itLines - 32;
    copyDistRange(from, to, set, size);

    // Copy interrupt configuration registers (ICFGRn)
    set = GicV2::GICD_ICFGR.start();
    size = GicV2::itLines / 4;
    copyDistRange(from, to, set, size);
}

void
MuxingKvmGic::fromGicV2ToKvm()
{
    copyGicState(static_cast<GicV2*>(this), kernelGic);
}

void
MuxingKvmGic::fromKvmToGicV2()
{
    copyGicState(kernelGic, static_cast<GicV2*>(this));

    // the values read for the Interrupt Priority Mask Register (PMR)
    // have been shifted by three bits due to its having been emulated by
    // a VGIC with only 5 PMR bits in its VMCR register.  Presently the
    // Linux kernel does not repair this inaccuracy, so we correct it here.
    for (int cpu = 0; cpu < system.numContexts(); ++cpu) {
       cpuPriority[cpu] <<= 3;
       assert((cpuPriority[cpu] & ~0xff) == 0);
    }
}

MuxingKvmGic *
MuxingKvmGicParams::create()
{
    return new MuxingKvmGic(this);
}
