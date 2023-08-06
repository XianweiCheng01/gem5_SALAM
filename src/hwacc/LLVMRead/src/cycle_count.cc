//------------------------------------------//
#include "cycle_count.hh"
//------------------------------------------//

CycleCounts::CycleCounts(CycleCountsParams *p) :
    SimObject(p),
    counter_inst(p->counter),
    gep_inst(p->gep),
    phi_inst(p->phi),
    select_inst(p->select),
    ret_inst(p->ret),
    br_inst(p->br),
    switch_inst(p->switch_inst),
    indirectbr_inst(p->indirectbr),
    invoke_inst(p->invoke),
    resume_inst(p->resume),
    unreachable_inst(p->unreachable),
    icmp_inst(p->icmp),
    fcmp_inst(p->fcmp),
    trunc_inst(p->trunc),
    zext_inst(p->zext),
    sext_inst(p->sext),
    fptrunc_inst(p->fptrunc),
    fpext_inst(p->fpext),
    fptoui_inst(p->fptoui),
    fptosi_inst(p->fptosi),
    uitofp_inst(p->uitofp),
    ptrtoint_inst(p->ptrtoint),
    inttoptr_inst(p->inttoptr),
    bitcast_inst(p->bitcast),
    addrspacecast_inst(p->addrspacecast),
    call_inst(p->call),
    vaarg_inst(p->vaarg),
    landingpad_inst(p->landingpad),
    catchpad_inst(p->catchpad),
    alloca_inst(p->alloca),
    load_inst(p->load),
    store_inst(p->store),
    fence_inst(p->fence),
    cmpxchg_inst(p->cmpxchg),
    atomicrmw_inst(p->atomicrmw),
    extractvalue_inst(p->extractvalue),
    insertvalue_inst(p->insertvalue),
    extractelement_inst(p->extractelement),
    insertelement_inst(p->insertelement),
    shufflevector_inst(p->shufflevector),
    shl_inst(p->shl),
    lshr_inst(p->lshr),
    ashr_inst(p->ashr),
    and_inst(p->and_inst),
    or_inst(p->or_inst),
    xor_inst(p->xor_inst),
    add_inst(p->add),
    sub_inst(p->sub),
    mul_inst(p->mul),
    udiv_inst(p->udiv),
    sdiv_inst(p->sdiv),
    urem_inst(p->urem),
    srem_inst(p->srem),
    fadd_inst(p->fadd),
    fsub_inst(p->fsub),
    fmul_inst(p->fmul),
    fdiv_inst(p->fdiv),
    frem_inst(p->frem) { 

        std::cout << "Cycle Counts Loaded!\n";
    }


CycleCounts*
CycleCountsParams::create() {
    return new CycleCounts(this);
}
