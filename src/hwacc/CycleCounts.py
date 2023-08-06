from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject

#Cycle Counts
class CycleCounts(SimObject):
    type = "CycleCounts"
    cxx_header = "hwacc/LLVMRead/src/cycle_count.hh"
    
    counter = Param.UInt32(1, "Counter intructions runtime cycles.")
    gep = Param.UInt32(1, "GetElementPtr intructions runtime cycles.")
    phi = Param.UInt32(1, "Phi intructions runtime cycles.")
    select = Param.UInt32(1, "Select intructions runtime cycles.")
    ret = Param.UInt32(1, "Return intructions runtime cycles.")
    br = Param.UInt32(1, "Branch intructions runtime cycles.")
    switch_inst = Param.UInt32(1, "Switch intructions runtime cycles.")
    indirectbr = Param.UInt32(1, "Indirect Branch intructions runtime cycles.")
    invoke = Param.UInt32(1, "Invoke intructions runtime cycles.")
    resume = Param.UInt32(1, "Resume intructions runtime cycles.")
    unreachable = Param.UInt32(1, "Unreachable intructions runtime cycles.")
    icmp = Param.UInt32(1, "Integer compare intructions runtime cycles.")
    fcmp = Param.UInt32(1, "Floating point compare intructions runtime cycles.")
    trunc = Param.UInt32(1, "Truncate intructions runtime cycles.")
    zext = Param.UInt32(1, "Zero extend intructions runtime cycles.")
    sext = Param.UInt32(1, "Sign extend intructions runtime cycles.")
    fptrunc = Param.UInt32(1, "Floating point truncate intructions runtime cycles.")
    fpext = Param.UInt32(1, "Floating point extend intructions runtime cycles.")
    fptoui = Param.UInt32(1, "Floating point to unsigned integer intructions runtime cycles.")
    fptosi = Param.UInt32(1, "Floating point to signed integer intructions runtime cycles.")
    uitofp = Param.UInt32(1, "Unsigned integer to floating point intructions runtime cycles.")
    ptrtoint = Param.UInt32(1, "Pointer to integer intructions runtime cycles.")
    inttoptr = Param.UInt32(1, "Integer to pointer intructions runtime cycles.")
    bitcast = Param.UInt32(1, "Bitcast intructions runtime cycles.")
    addrspacecast = Param.UInt32(1, "Address space cast intructions runtime cycles.")
    call = Param.UInt32(1, "Call intructions runtime cycles.")
    vaarg = Param.UInt32(1, "Vaarg intructions runtime cycles.")
    landingpad = Param.UInt32(1, "Landing pad intructions runtime cycles.")
    catchpad = Param.UInt32(1, "Catch pad intructions runtime cycles.")
    alloca = Param.UInt32(1, "Allocate intructions runtime cycles.")
    load = Param.UInt32(0, "Must be 0, handled by memory controller");
    store = Param.UInt32(0, "Must be 0, handled by memory controller");
    fence = Param.UInt32(1, "Fence intructions runtime cycles.")
    cmpxchg = Param.UInt32(1, "Compare and exchange intructions runtime cycles.")
    atomicrmw = Param.UInt32(1, "Atomic remove intructions runtime cycles.")
    extractvalue = Param.UInt32(1, "Extract value intructions runtime cycles.")
    insertvalue = Param.UInt32(1, "Insert value intructions runtime cycles.")
    extractelement = Param.UInt32(1, "Extract element intructions runtime cycles.")
    insertelement = Param.UInt32(1, "Insert element intructions runtime cycles.")
    shufflevector = Param.UInt32(1, "Shuffle vector intructions runtime cycles.")
    shl = Param.UInt32(1, "Shift left intructions runtime cycles.")
    lshr = Param.UInt32(1, "Logical shift right intructions runtime cycles.")
    ashr = Param.UInt32(1, "Arithmetic shift right intructions runtime cycles.")
    and_inst = Param.UInt32(1, "And intructions runtime cycles.")
    or_inst = Param.UInt32(1, "Or intructions runtime cycles.")
    xor_inst = Param.UInt32(1, "Xor intructions runtime cycles.")
    add = Param.UInt32(1, "Integer add intructions runtime cycles.")
    sub = Param.UInt32(1, "Integer subtract intructions runtime cycles.")
    mul = Param.UInt32(1, "Integer multiply intructions runtime cycles.")
    udiv = Param.UInt32(1, "Unsigned integer division intructions runtime cycles.")
    sdiv = Param.UInt32(1, "Signed integer division intructions runtime cycles.")
    urem = Param.UInt32(1, "Unsigned remainder intructions runtime cycles.")
    srem = Param.UInt32(1, "Signed remainder intructions runtime cycles.")
    fadd = Param.UInt32(5, "Floating point addition intructions runtime cycles.")
    fsub = Param.UInt32(5, "Floating point subtraction intructions runtime cycles.")
    fmul = Param.UInt32(4, "Floating point multiplication intructions runtime cycles.")
    fdiv = Param.UInt32(16, "Floating point division intructions runtime cycles.")
    frem = Param.UInt32(5, "Floating point remainder intructions runtime cycles.")

    
    
    
