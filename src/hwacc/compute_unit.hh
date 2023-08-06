#ifndef __HWACC_COMPUTE_UNIT_HH__
#define __HWACC_COMPUTE_UNIT_HH__
//------------------------------------------//
#include "params/ComputeUnit.hh"
#include "sim/sim_object.hh"
#include "hwacc/comm_interface.hh"
#include "hwacc/LLVMRead/src/mem_request.hh"
#include "hwacc/LLVMRead/src/debug_flags.hh"
#include "hwacc/LLVMRead/src/cycle_count.hh"
//------------------------------------------//

class ComputeUnit : public SimObject {
  private:

  protected:
    CommInterface *comm;
    CycleCounts *cycles;

    class TickEvent : public Event
    {
      private:
        ComputeUnit *comp_unit;

      public:
        TickEvent(ComputeUnit *_comp_unit) : Event(CPU_Tick_Pri), comp_unit(_comp_unit) {}
        void process() { comp_unit->tick(); }
        virtual const char *description() const { return "ComputeUnit tick"; }
    };

    TickEvent tickEvent;
    int clock_period;

  public:
    virtual void tick() {}
    ComputeUnit(ComputeUnitParams *p);
    virtual void initialize() {}
    virtual void readCommit(MemoryRequest * req) {}
    virtual void writeCommit(MemoryRequest * req) {}
};

#endif //__HWACC_COMPUTE_UNIT_HH__
