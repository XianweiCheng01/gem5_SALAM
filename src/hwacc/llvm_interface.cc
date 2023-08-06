//------------------------------------------//
#include "hwacc/llvm_interface.hh"
//------------------------------------------//

LLVMInterface::LLVMInterface(LLVMInterfaceParams *p) :
    ComputeUnit(p),
    filename(p->in_file),
    lockstep(p->lockstep_mode),
    scheduling_threshold(p->sched_threshold),
    counter_units(p->FU_counter),
    int_adder_units(p->FU_int_adder),
    int_multiply_units(p->FU_int_multiplier),
    int_shifter_units(p->FU_int_shifter),
    int_bit_units(p->FU_int_bit),
    fp_sp_adder(p->FU_fp_sp_adder),
    fp_dp_adder(p->FU_fp_dp_adder),
    fp_sp_multiply(p->FU_fp_sp_multiplier),
    fp_sp_division(p->FU_fp_sp_divider),
    fp_dp_multiply(p->FU_fp_dp_multiplier),
    fp_dp_division(p->FU_fp_dp_divider),
    compare(p->FU_compare),
    gep(p->FU_GEP),
    conversion(p->FU_conversion),
    pipelined(p->FU_pipelined),
    fu_latency(p->clock_period),
    clock_period(p->clock_period) {
    bbList = NULL;
    regList = NULL;
    currBB = NULL;
    prevBB = NULL;
    typeList = NULL;
    running = false;
    memory_loads = 0;
    memory_stores = 0;
    dma_loads = 0;
    dma_stores = 0;
    global_loads = 0;
    global_stores = 0;
    clock_period = clock_period * 1000;
    //process_delay = 1; //Number of cycles a compute_node needs to complete
}

std::shared_ptr<InstructionBase> createClone(const std::shared_ptr<InstructionBase>& b) {
    std::shared_ptr<InstructionBase> clone = b->clone();
    return clone;
}

void
LLVMInterface::tick() {
/*********************************************************************************************
 CN Scheduling

 As CNs are scheduled they are added to an in-flight queue depending on operation type.
 Loads and Stores are maintained in separate queues, and are committed by the comm_interface.
 Branch and phi instructions evaluate and commit immediately. All other CN types are added to
 an in-flight compute queue.

 Each tick we must first check our in-flight compute queue. Each node should have its cycle
 count incremented, and should commit if max cycle is reached.

 New CNs are added to the reservation table whenever a new BB is encountered. This may occur
 during device init, or when a br op commits. For each CN in a BB we reset the CN, evaluate
 if it is a phi or uncond br, and add it to our reservation table otherwise.
*********************************************************************************************/
    bool dbg = debug();
    if (dbg) {
        DPRINTF(LLVMInterface, "\n%s\n%s %d\n%s\n",
            "********************************************************************************",
            "   Cycle", cycle,
            "********************************************************************************");
    }
    cycle++;
    comm->refreshMemPorts();
    hardware->update();
    ///////////////////////////
    pwrUtil->updatePowerConsumption(_FunctionalUnits);
    regList->resetAccess();
    clearFU();
    loadOpScheduled = false;
    storeOpScheduled = false;
    compOpScheduled = false;
    loadInFlight = readQueue.size();
    storeInFlight = writeQueue.size();
    compInFlight = computeQueue.size();
    ///////////////////////////
    if (dbg) DPRINTF(LLVMInterface, "Queue In-Flight Status: Cmp:%d Rd:%d Wr:%d\n", computeQueue.size(), readQueue.size(), writeQueue.size());
    //Check our compute queue to see if any compute nodes are ready to commit
    if (dbg) DPRINTF(LLVMInterface, "Checking Compute Queue for Nodes Ready for Commit!\n");
    for(auto i = 0; i < computeQueue.size();) {
        if (dbg) DPRINTF(LLVMOp, "Checking if %s has finished\n", computeQueue.at(i)->_OpCode);
        if(pipelined) {
             if(unlimitedFU) {
                updateFU(reservation.at(i)->_FunctionalUnit);
                if(computeQueue.at(i)->commit()) {
                    computeQueue.at(i)->removeParents();
                    if(SHAREDDEBUG) std::cout << "Ptr Refs: " << computeQueue.at(i)->name() << " | " << computeQueue.at(i).use_count() << "\n";
                    auto it = computeQueue.erase(computeQueue.begin() + i);
                    i = std::distance(computeQueue.begin(), it);
                }
                else i++;
            } else if(limitedFU(reservation.at(i)->_FunctionalUnit)) {
                if(computeQueue.at(i)->commit()) {
                    computeQueue.at(i)->removeParents();
                    if(SHAREDDEBUG) std::cout << "Ptr Refs: " << computeQueue.at(i)->name() << " | " << computeQueue.at(i).use_count() << "\n";
                    auto it = computeQueue.erase(computeQueue.begin() + i);
                    i = std::distance(computeQueue.begin(), it);
                } else i++;
            } else i++;
        } else { 
            if(unlimitedFU) {
                updateFU(reservation.at(i)->_FunctionalUnit);
                if(computeQueue.at(i)->commit()) {
                    computeQueue.at(i)->removeParents();
                    if(SHAREDDEBUG) std::cout << "Ptr Refs: " << computeQueue.at(i)->name() << " | " << computeQueue.at(i).use_count() << "\n";
                    auto it = computeQueue.erase(computeQueue.begin() + i);
                    i = std::distance(computeQueue.begin(), it);
                }
                else i++;
            } else if(limitedFU(reservation.at(i)->_FunctionalUnit)) {
                if(computeQueue.at(i)->commit()) {
                    computeQueue.at(i)->removeParents();
                    if(SHAREDDEBUG) std::cout << "Ptr Refs: " << computeQueue.at(i)->name() << " | " << computeQueue.at(i).use_count() << "\n";
                    auto it = computeQueue.erase(computeQueue.begin() + i);
                    i = std::distance(computeQueue.begin(), it);
                } else i++;
            } else i++;
        }
    }
    scheduled = false;
    if (reservation.empty()) { // If no compute nodes in reservation queue, load next basic block
        if (dbg) DPRINTF(LLVMInterface, "Schedule Basic Block!\n");
        scheduleBB(currBB);
    }

    if (lockstep && (!computeQueue.empty() || !readQueue.empty() || !writeQueue.empty() )) {
        //Do nothing
    } else {
        for (auto i = 0; i < reservation.size();) {
            if (reservation.at(i)->_ReturnRegister == NULL) {
                if (dbg) DPRINTF(RuntimeQueues, "Checking if %s can launch\n", reservation.at(i)->_OpCode);
            } else {
                if (dbg) DPRINTF(RuntimeQueues, "Checking if %s returning to %s can launch\n", reservation.at(i)->_OpCode, reservation.at(i)->_ReturnRegister->getName());
            }
            if (reservation.at(i)->_ActiveParents == 0) {
                if(!(reservation.at(i)->_Terminator)) { 
                        if(reservation.at(i)->_OpCode == "load") {
                            loadOpScheduled = true;
                            readQueue.push_back(reservation.at(i));
                            reservation.at(i)->compute();
                            if(reservation.at(i)->_Global) dma_loads++;
                            else memory_loads++; 
                            if(unlimitedFU) updateFU(reservation.at(i)->_FunctionalUnit);
                            scheduled = true;
                            //  reservation.at(i)->removeParents();
                            auto it = reservation.erase(reservation.begin()+i);
                            i = std::distance(reservation.begin(), it);
                        } else if(reservation.at(i)->_OpCode == "store") {
                            storeOpScheduled = true;
                            writeQueue.push_back(reservation.at(i));
                            reservation.at(i)->compute();
                            if(reservation.at(i)->_Global) dma_stores++;
                            else memory_stores++;
                            if(unlimitedFU) updateFU(reservation.at(i)->_FunctionalUnit);
                            scheduled = true;
                            //  reservation.at(i)->removeParents();
                            auto it = reservation.erase(reservation.begin()+i);
                            i = std::distance(reservation.begin(), it);
                        } else if(reservation.at(i)->_MaxCycle==0) {
                            reservation.at(i)->compute();
                            reservation.at(i)->commit(); 
                            if(unlimitedFU) updateFU(reservation.at(i)->_FunctionalUnit);
                            scheduled = true; compOpScheduled = true;
                            reservation.at(i)->removeParents();
                            auto it = reservation.erase(reservation.begin()+i);
                            i = std::distance(reservation.begin(), it);
                        } else { // Computation Units
                            if(!unlimitedFU){
                                if(limitedFU(reservation.at(i)->_FunctionalUnit)){
                                    if(reservation.at(i)->_OpCode == "fdiv") _FunctionalUnits.fpDivision++;
                                    computeQueue.push_back(reservation.at(i));
                                    reservation.at(i)->compute();
                                    reservation.at(i)->commit();
                                    scheduled = true; compOpScheduled = true;
                                    auto it = reservation.erase(reservation.begin()+i);
                                    i = std::distance(reservation.begin(), it);
                                } else i++;
                            }
                            else {
                                computeQueue.push_back(reservation.at(i));
                                reservation.at(i)->compute();
                                reservation.at(i)->commit();
                                if(unlimitedFU) updateFU(reservation.at(i)->_FunctionalUnit);
                                scheduled = true; compOpScheduled = true;
                                auto it = reservation.erase(reservation.begin()+i);
                                i = std::distance(reservation.begin(), it);
                            }
                        }
                } else if ((reservation.at(i)->_OpCode != "ret")) {
                    if (reservation.size() < scheduling_threshold) {
                        if(unlimitedFU) updateFU(reservation.at(i)->_FunctionalUnit);
                        prevBB = currBB; // Store current BB as previous BB for use with Phi instructions
                        reservation.at(i)->compute(); // Send instruction to runtime computation simulator 
                        currBB = findBB(reservation.at(i)->_Dest); // Set pointer to next basic block
                        //  reservation.at(i)->removeParents();
                        auto it = reservation.erase(reservation.begin()+i); // Remove instruction from reservation table
                        i = std::distance(reservation.begin(), it);
                        scheduleBB(currBB);
                    } else i++;
                }
            } else {
                if (reservation.at(i)->_OpCode == "ret"){
                    if (i==0 && computeQueue.empty() && readQueue.empty() && writeQueue.empty()) {
                      auto it = reservation.erase(reservation.begin() + i);
                      if (dbg)
                        DPRINTF(LLVMInterface, "Simulation Complete \n");
                      running = false;
                      finalize();
                      comm->finish();
                    }
                }
                i++;
            }
        }
    }
    occupancy();

    if (running && !tickEvent.scheduled())
    {
        schedule(tickEvent, curTick() + clock_period);// * process_delay);
    }
}

void
LLVMInterface::scheduleBB(BasicBlock* bb) {
/*********************************************************************************************
 BB Scheduling

 As BBs are scheduled they are added to the reservation table. Once the BB is scheduled check
 if the the BB includes Phi instructions, and if so commit them immediately. Also checks if
 the only remaining instructions are unconditional branches such as pre-headers and crit-edges
 and immediately compute and commit.
*********************************************************************************************/
    if (debug()) DPRINTF(LLVMInterface, "Adding BB: (%s) to Reservation Table!\n", bb->getName());
    for (auto i = 0; i < bb->_Nodes.size(); i++) {
        if (debug()) DPRINTF(LLVMOp, "Adding %s to reservation table\n", bb->_Nodes.at(i)->_OpCode);
        reservation.push_back(createClone(bb->_Nodes.at(i)));
        if (reservation.back()->_ReturnRegister) { //Search for other instances of the same instruction
            std::shared_ptr<InstructionBase> parent = findParent(reservation.back()->_LLVMLine);
            if (parent) {
                if (debug()) DPRINTF(LLVMOp, "Previous instance found\n");
                reservation.back()->registerParent(parent);
                parent->registerChild(reservation.back());
            } else {
                if (debug()) DPRINTF(LLVMOp, "No previous instance found\n");
            }
        }
        if (reservation.back()->_OpCode == "load") {
            std::shared_ptr<InstructionBase> parent = detectRAW(static_pointer_cast<Load>(reservation.back())->_RawCheck);
            if (parent) {
                if (debug()) DPRINTF(LLVMOp, "Memory RAW dependency corrected!\n");
                reservation.back()->registerParent(parent);
                parent->registerChild(reservation.back());
            } else {
                if (debug()) DPRINTF(LLVMOp, "No RAW dependency found\n");
            }
        }
        if (reservation.back()->_OpCode == "getelementptr") {
            std::shared_ptr<InstructionBase> parent = findParent(std::static_pointer_cast<GetElementPtr>(reservation.back())->_PtrVal);
            if (parent) {
                if (debug()) DPRINTF(LLVMOp, "Parent returning to base register:%s found\n", std::static_pointer_cast<GetElementPtr>(reservation.back())->_PtrVal->getName());
                reservation.back()->registerParent(parent);
                parent->registerChild(reservation.back());
            } else {
                if (debug()) DPRINTF(LLVMOp, "No parent returning to base register:%s found\n", std::static_pointer_cast<GetElementPtr>(reservation.back())->_PtrVal->getName());
                std::static_pointer_cast<GetElementPtr>(reservation.back())->_ActivePtr = std::static_pointer_cast<GetElementPtr>(reservation.back())->_PtrVal->getValue();
            }
        }
        std::vector<Register*> depList = reservation.back()->runtimeDependencies(prevBB->getName());
        if (depList.size() > 0) {
            for (auto j = 0; j < depList.size(); j++) { //Search for parent nodes in scheduling and in-flight queues
                if (depList.at(j)!=NULL) {
                    std::shared_ptr<InstructionBase> parent = findParent(depList.at(j));
                    if (parent) {
                        if (debug()) DPRINTF(LLVMOp, "Parent returning to register:%s found\n", depList.at(j)->getName());
                        reservation.back()->registerParent(parent);
                        parent->registerChild(reservation.back());
                    } else {
                        if (debug()) DPRINTF(LLVMOp, "No parent returning to register:%s found\n", depList.at(j)->getName());
                        reservation.back()->fetchDependency(j);
                    }
                }
            }
        }
    }
    if (debug()) DPRINTF(LLVMInterface, "Adding BB: Complete!\n");
    if (debug()) DPRINTF(RuntimeQueues, "Active Scheduling Window\n");
    for (auto i = 0; i < reservation.size(); i++) {
        if (reservation.at(i)->_ReturnRegister == NULL) {
            if (debug()) DPRINTF(RuntimeQueues, "%s\n", reservation.at(i)->_OpCode);
        } else {
            if (debug()) DPRINTF(RuntimeQueues, "%s %s\n", reservation.at(i)->_OpCode, reservation.at(i)->_ReturnRegister->getName());
        }
    }
}

std::shared_ptr<InstructionBase>
LLVMInterface::detectRAW(Register* reg) {
    for(int i = reservation.size()-2; i >= 0; i--) { //Start with the second to last node to avoid linking a node to itself
        if(reservation.at(i)->_OpCode == "store") {
            if(reservation.at(i)->_RawCheck == reg) return reservation.at(i);
        }
    }
    for(int i = writeQueue.size()-1; i >= 0; i--) {
        if (writeQueue.at(i)->_RawCheck == reg) return writeQueue.at(i);
    }
    return NULL;
}

std::shared_ptr<InstructionBase>
LLVMInterface::findParent(Register* reg) {
    //Search queues with return registers for last instance of a node with the same return register as our target reg
    //Check the reservation queue first to ensure we get the last instance if multiple instances exist in queues
    for(int i = reservation.size()-2; i >= 0; i--) { //Start with the second to last node to avoid linking a node to itself
        if (reservation.at(i)->_ReturnRegister == reg) return reservation.at(i);
    }
    for(int i = computeQueue.size()-1; i >= 0; i--) {
        if (computeQueue.at(i)->_ReturnRegister == reg) return computeQueue.at(i);
    }
    for(int i = readQueue.size()-1; i >= 0; i--) {
        if (readQueue.at(i)->_ReturnRegister == reg) return readQueue.at(i);
    }
    return NULL;
}

std::shared_ptr<InstructionBase>
LLVMInterface::findParent(std::string line) {
    //Search queues with return registers for last instance of a node with the same instruction
    //Check the reservation queue first to ensure we get the last instance if multiple instances exist in queues
    for(int i = reservation.size()-2; i >= 0; i--) { //Start with the second to last node to avoid linking a node to itself
        if (reservation.at(i)->_LLVMLine == line) return reservation.at(i);
    }
    for(int i = computeQueue.size()-1; i >= 0; i--) {
        if (computeQueue.at(i)->_LLVMLine == line) return computeQueue.at(i);
    }
    for(int i = readQueue.size()-1; i >= 0; i--) {
        if (readQueue.at(i)->_LLVMLine == line) return readQueue.at(i);
    }
    return NULL;
}



void
LLVMInterface::constructBBList() {
/*********************************************************************************************
 Constructing Basic Block List

 Parses LLVM file and creates the CDFG passed to our runtime simulation engine.
*********************************************************************************************/
    // llvm::Module * m = llvm::parseIRFile(filename, error, context).get();
    // m->dump();
    bool dbg = debug();
    if (dbg) DPRINTF(LLVMInterface, "Constructing Static Dependency Graph\n");
    initFU();
    bbList = new std::list<BasicBlock*>(); // Create New Basic Block List
    regList = new RegisterList(); // Create New Register List
    typeList = new TypeList(); // Create New User Defined Types List
    Register* alwaysTrue = new Register("alwaysTrue", ((uint64_t) 1));
    Register* alwaysFalse = new Register("alwaysFalse", ((uint64_t) 0));
    regList->addRegister(alwaysTrue);
    regList->addRegister(alwaysFalse);
    pwrUtil = new Utilization(clock_period, fu_clock_period, regList);
    pwrUtil->setFPReuseFactor(1);
    if (occupancy_tracking) _CycleOccList = new std::list<FunctionalUnits>();
    hardware = new Hardware(fu_latency); // Initialize Hardware Functional Units
    hardware->linkRegList(regList);
    std::ifstream llvmFile(filename, std::ifstream::in); // Open LLVM File
    std::string line; // Stores Single Line of File
    bool inFunction = false; // Parse Variable
    unsigned bbnum = 0; // Start of Basic Block Numbering
    if (dbg) DPRINTF(LLVMInterface, "Parsing: (%s)\n", filename);
    if(llvmFile.is_open()) {
        while (getline(llvmFile, line)) { // Read until end of LLVM file
            if (dbg) DPRINTF(LLVMParse, "Line: (%s)\n", line);
            if (!inFunction) { // Looks for data before the main function is defined
                if (!line.find("%struct")) { // Found custom data type.
                    int pos = line.find('=');
                    int size = 1; // Size indicated number of elements in custom data type
                    std::string name = line.substr(1,pos-2); // Store name as called within function
                    for(int i = pos; i < line.size(); i++) {
                        if(line[i] == ',') size++; // Elements delimeted by commas, counts number of elements
                    }
                    typeList->addType(new LLVMType(size, name)); // Add custom data type to typeList
                } else if (!line.find("define")) { //Found a function. Need to parse its header
                    if (dbg) DPRINTF(LLVMParse, "Found ACC Function, Parsing Global Variables!\n");
                    inFunction = true;
                    unsigned paramOffset = 0;
                    unsigned linePos = 0;
                    int percPos = line.find("%"); //All registers preceeded by a % in LLVM
                    int commaPos;
                    while (percPos > -1) {
                        // Parse all global variables within the function definition
                        if (line.find("%struct", linePos) != percPos) { //Ensure we didn't just find a struct type
                            percPos++;
                            commaPos = line.find(",", percPos);
                            if (commaPos < 0) commaPos = line.find(")");
                            std::string regName = line.substr(percPos, (commaPos-percPos)); // Determine register name for global variable
                            if (dbg) DPRINTF(LLVMParse, "Creating register for: (%s)\n", regName);
                            regList->addRegister(new Register(regName, comm->getGlobalVar(paramOffset, 8))); // Create register for global variable
                            if (dbg) DPRINTF(LLVMParse, "Initial Value: (%X)\n", (regList->findRegister(regName))->getValue());
                            paramOffset+=8;
                        }
                        linePos = percPos + 1;
                        percPos = line.find("%", linePos); // Check if another register exists within the function definition
                    }
                    streampos oldpos = llvmFile.tellg();
                    getline(llvmFile, line);
                    if (line.find(':') != std::string::npos) {
                    std::string first_bb = line.substr(0, line.find(':'));
                    currBB = new BasicBlock(first_bb, name(), bbnum, dbg); // First basic block is always defined as BB 0
                    if (dbg) DPRINTF(LLVMParse, "Found Basic Block: (%s)\n", currBB->_Name);
                    bbnum++; // Increment BB count
                    bbList->push_back(currBB); // Add BB to BB list
                    } else {
                    currBB = new BasicBlock("0", name(), bbnum, dbg); // First basic block is always defined as BB 0
                    if (dbg) DPRINTF(LLVMParse, "Found Basic Block: (%s)\n", currBB->_Name);
                    bbnum++; // Increment BB count
                    bbList->push_back(currBB); // Add BB to BB list
                    llvmFile.seekg (oldpos);
                    }
                }
            } else { // Already within a function, begin defining basic blocks and compute nodes
                if (line.find("\n") > 0) { // Skip blank lines
                    if (line.find("; <label>:") == 0) { // Found new basic block
                        int labelEnd = line.find(" ", 10);
                        prevBB = currBB; // Set previous basic block
                       // currBB->printNodes();  ////////////////////////////////////////////////////////////////////////////////////////////
                        // LLVM Version 3.8 added a new : after label names, check for this
                        std::string versionCheck = line.substr(10,(labelEnd - 10));
                        if(versionCheck.back() == ':') versionCheck = line.substr(10,(labelEnd - 11));
                        currBB = new BasicBlock(versionCheck, name(), bbnum, dbg); // Create new basic block
                        if (dbg) DPRINTF(LLVMParse, "Found Basic Block: (%s)\n", currBB->_Name);
                        bbnum++; // Increment BB count
                        bbList->push_back(currBB); // Add BB to BB list
                    } else if (line.find("; preds") != std::string::npos) { // Found new basic block (edge)
                        int labelEnd = line.find(" ");
                        prevBB = currBB; // Set previous basic block
                        currBB = new BasicBlock(line.substr(0,(labelEnd-1)), name(), bbnum, dbg); // Create new basic block
                        if (dbg) DPRINTF(LLVMParse, "Found Basic Block: (%s)\n", currBB->_Name);
                        bbnum++; // Increment BB count
                        bbList->push_back(currBB); // Add BB to BB list
                    }else if (line.find("}") == 0) { // Found end of function definition
                        inFunction = false;
                        if (dbg) DPRINTF(LLVMParse, "Finished File Parsing!\n");
                        break;
                    } else if (!(line.find_first_not_of(' ') != std::string::npos)){ // Skip empty Line
                    } else { // Found instruction, create new compute node within the current BB
                        if (dbg) DPRINTF(LLVMParse, "Registering Compute Node for: (%s)\n", line);
                        if ((line.find("switch") != -1)) {
                            // If instruction is switch statement, convert it to be defined in a single line
                            if (dbg) DPRINTF(LLVMParse, "Found Switch Statement, Converting to Inline!\n");
                            std::string concatLine;
                            int cases = 1;
                            while(line.find(" ]") == -1) {
                                // Continue reading through lines until the end of switch statement
                                // Concatinate all instruction lines into a single string
                                if (dbg) DPRINTF(LLVMParse, "Case %d: (%s)\n", cases, line);
                                concatLine += line;
                                getline(llvmFile, line);
                                cases++;
                            }
                        concatLine+= " ]"; // Close case statements arguement
                        line = concatLine;
                        if (dbg) DPRINTF(LLVMParse, "New Switch Instruction Line: (%s)\n", line);
                        }
                        if(prevBB) { // Add instruction line to compute node list in current BB
                            hardware->updateParsed(currBB->parse(line, regList, prevBB->getName(), comm, typeList, cycles, pipelined));
                        } else { // Add instruction line to compute node list in current BB (Fist BB Only)
                            hardware->updateParsed(currBB->parse(line, regList, "NULL", comm, typeList, cycles, pipelined));
                        }

                    }
                }
            }
        }
        currBB = findEntryBB(); //
        prevBB = currBB;
    } else { // Could not find LLVM file
        panic("Unable to open LLVM file!\n");
    }
    setupStop = std::chrono::high_resolution_clock::now();
    setupTime = std::chrono::duration_cast<std::chrono::duration<double>>(setupStop-timeStart);
}

BasicBlock*
LLVMInterface::findBB(std::string bbname) {
/*********************************************************************************************
 Find Basic Block

 Iterates through list of known basic block and returns a pointer to passed BB name if found
 and NULL if the BB does not exist in the BB list.
*********************************************************************************************/
    for (auto it = bbList->begin(); it != bbList->end(); ++it) {
        if ((*it)->_Name.compare(bbname) == 0)
            return (*it);
    }
    return NULL;
}

BasicBlock*
LLVMInterface::findEntryBB() {
/*********************************************************************************************
 Find Entry Basic Block

 Iterates through list of known basic block and returns a pointer to the first basic block
 that contains instructions
*********************************************************************************************/
    for (auto it = bbList->begin(); it != bbList->end(); ++it) {
        if (!((*it)->isEmpty()))
            return (*it);
    }
    panic("No entry basic block found!\n");
    return NULL;
}

void
LLVMInterface::readCommit(MemoryRequest * req) {
/*********************************************************************************************
 Commit Memory Read Request
*********************************************************************************************/
    for (auto i = 0; i < readQueue.size(); i++ ) {
        if(readQueue.at(i)->getReq() == req) {
            readQueue.at(i)->setResult(req->buffer);
            readQueue.at(i)->commit();
            readQueue.at(i)->removeParents();
            if(SHAREDDEBUG) std::cout << "Ptr Refs: " << readQueue.at(i)->name() << " | " << readQueue.at(i).use_count() << "\n";
            readQueue.erase(readQueue.begin() + i);
        }
    }
}

void
LLVMInterface::writeCommit(MemoryRequest * req) {
/*********************************************************************************************
 Commit Memory Write Request
*********************************************************************************************/
   for (auto i = 0; i < writeQueue.size(); i++ ) {
        if(writeQueue.at(i)->getReq() == req) {
            writeQueue.at(i)->commit();
            writeQueue.at(i)->removeParents();
            if(SHAREDDEBUG) std::cout << "Ptr Refs: " << writeQueue.at(i)->name() << " | " << writeQueue.at(i).use_count() << "\n";
            writeQueue.erase(writeQueue.begin() + i);
        }
    }
}

void
LLVMInterface::initialize() {
/*********************************************************************************************
 Initialize the Runtime Engine

 Calls function that constructs the basic block list, initializes the reservation table and
 read, write, and compute queues. Set all data collection variables to zero.
*********************************************************************************************/
    timeStart = std::chrono::high_resolution_clock::now();
    if (debug()) DPRINTF(LLVMInterface, "Initializing LLVM Runtime Engine!\n");
    constructBBList();
    if (debug()) DPRINTF(LLVMInterface, "Initializing Reservation Table!\n");
    if (debug()) DPRINTF(LLVMInterface, "Initializing readQueue Queue!\n");
    if (debug()) DPRINTF(LLVMInterface, "Initializing writeQueue Queue!\n");
    if (debug()) DPRINTF(LLVMInterface, "Initializing computeQueue List!\n");
    if (debug()) DPRINTF(LLVMInterface, "\n%s\n%s\n%s\n",
            "*******************************************************************************",
            "*                 Begin Runtime Simulation Computation Engine                 *",
            "*******************************************************************************");
    running = true;
    cycle = 0;
    stalls = 0;
    execnodes = 0;
    loadOnly = 0;
    storeOnly = 0;
    compOnly = 0;
    loadStore = 0;
    loadComp = 0;
    loadStoreComp = 0;
    storeComp = 0;
    loadOnlyStall = 0; 
    storeOnlyStall = 0; 
    compOnlyStall = 0; 
    loadStoreStall = 0; 
    loadCompStall = 0;
    loadStoreCompStall = 0;
    storeCompStall = 0;
    unlimitedFU = unlimitedMode(); // If all functional units are unlimited, decreases simulation time
    hardware->updateLimit(counter_units,
                                int_adder_units,
                                int_multiply_units,
                                int_shifter_units,
                                int_bit_units,
                                fp_sp_adder,
                                fp_dp_adder,
                                fp_sp_multiply,
                                fp_sp_division,
                                fp_dp_multiply,
                                fp_dp_division,
                                compare,
                                gep,
                                conversion);
    if(pipelined) hardware->pipelined();
    tick();
}


void
LLVMInterface::startup() {
/*********************************************************************************************
 Initialize communications between gem5 interface and simulator
*********************************************************************************************/
    comm->registerCompUnit(this);
}

LLVMInterface*
LLVMInterfaceParams::create() {
/*********************************************************************************************
 Create new interface between the llvm IR and our simulation engine
*********************************************************************************************/
    return new LLVMInterface(this);
}

bool
LLVMInterface::unlimitedMode() {
    if((counter_units == -1) &&
        (int_adder_units == -1) &&
        (int_multiply_units == -1) &&
        (int_shifter_units == -1) &&
        (int_bit_units == -1) &&
        (fp_sp_adder == -1) &&
        (fp_dp_adder == -1) &&
        (fp_sp_multiply == -1) &&
        (fp_sp_division == -1) &&
        (fp_dp_multiply == -1) &&
        (fp_dp_division == -1) &&
        (compare == -1) &&
        (gep == -1)) return true;
    return false;
}

void
LLVMInterface::occupancy() {
    if (loadOpScheduled) {
        if (storeOpScheduled) {
            if (compOpScheduled) loadStoreComp++;
            else loadStore++;
        } else if (compOpScheduled) loadComp++;
        else loadOnly++;
    } else if(storeOpScheduled) {
        if (compOpScheduled) storeComp++;
        else storeOnly++;
    } else if (compOpScheduled) compOnly++;
    else {
        stalls++;
        if(loadInFlight) {
            if(storeInFlight) {
                if(compInFlight) loadStoreCompStall++;
                else loadStoreStall++;
            } else if (compInFlight) loadCompStall++;
            else loadOnlyStall++;
        } else if(storeInFlight) {
            if(compInFlight) storeCompStall++;
            else storeOnlyStall++;
        } else if (compInFlight) compOnlyStall++;
    }
}

void
LLVMInterface::finalize() {
    std::cout << "1\n";
    // Simulation Times
    running = false;
    simStop = std::chrono::high_resolution_clock::now();
    simTime = std::chrono::duration_cast<std::chrono::duration<double>>(simStop-timeStart);
//    cache_size = comm->getCacheSize();
    read_ports = comm->getReadPorts();
    write_ports = comm->getWritePorts();
    spm_size = comm->getPmemRange();
    read_bus_width = comm->getReadBusWidth();
    write_bus_width = comm->getWriteBusWidth();
//    cache_ports = comm->getcachePorts();
//    local_ports = comm->getlocalPorts();
    runtime = (cycle*(1e-10));
    execnodes = cycle-stalls-1;
    AverageOccupancy();
    pwrUtil->finalPowerUsage(_MaxFU, cycle);
   
    /*
    // Experimental
    // getCactiResults(int cache_size, int word_size, int ports, int type)
    // SPM cache_type = 0  
    _SPM.opt = pwrUtil->getCactiResults(regList->count()*(read_bus_width), (read_bus_width/8), (read_ports+write_ports), 0);
    _SPM.leakage = pwrUtil->getCactiResults(spm_size, (read_bus_width/8), (read_ports+write_ports), 0);
    _SPM.dyn_read = pwrUtil->getCactiResults((int) (dma_loads*(read_bus_width/8)), (read_bus_width/8), (read_ports), 0); 
    _SPM.dyn_write = pwrUtil->getCactiResults((int) (dma_stores*(write_bus_width/8)), (read_bus_width/8), (write_ports), 0); 

    // Experimental
    // Cache cache_type = 1
    _Cache.leakage = pwrUtil->getCactiResults(cache_size, (read_bus_width/8), cache_ports, 1);
    _Cache.dyn_read = pwrUtil->getCactiResults(dma_loads*(read_bus_width/8), (read_bus_width/8), cache_ports, 1);
    _Cache.dyn_write = pwrUtil->getCactiResults(dma_stores*(read_bus_width/8), (read_bus_width/8), cache_ports, 1);
    
    */
    printPerformanceResults();
    // comm->printResults() 
    // hardware->printResults();
}

void
LLVMInterface::printPerformanceResults() {
    Tick cycle_time = clock_period/1000;
/*********************************************************************************************
 Prints usage statistics of how many times each instruction was accessed during runtime
*********************************************************************************************/
    std::cout << "********************************************************************************" << std::endl;
    std::cout << name() << std::endl;
    std::cout << "   ========= Performance Analysis =============" << std::endl;
    std::cout << "   Setup Time:                      " << (double)(setupTime.count()) << "seconds" << std::endl;
    std::cout << "   Simulation Time:                 " << (double)(simTime.count()) << "seconds" << std::endl;
    std::cout << "   System Clock:                    " << 1.0/(cycle_time) << "GHz" << std::endl;
    std::cout << "   Transistor Latency:              " << fu_latency << "ns" << std::endl;
    std::cout << "   Runtime:                         " << cycle << " cycles" << std::endl;
    std::cout << "   Runtime:                         " << (cycle*cycle_time*(1e-3)) << " us" << std::endl;
    std::cout << "   Stalls:                          " << stalls << " cycles" << std::endl;
    std::cout << "   Executed Nodes:                  " << (cycle-stalls-1) << " cycles" << std::endl;
    std::cout << std::endl;

    results = new Results(  (double)(setupTime.count()*exponential),
                            (double)(simTime.count()*exponential),
                            clock_period,
                            fu_clock_period,
                            cycle,
                            runtime,
                            stalls,
                            execnodes,
                            loadOnly,
                            storeOnly,
                            compOnly,
                            loadStore,
                            loadComp,
                            loadStoreComp,
                            storeComp,
                            loadOnlyStall,
                            storeOnlyStall,
                            compOnlyStall,
                            loadStoreStall,
                            loadCompStall,
                            loadStoreCompStall,
                            storeCompStall,
                            cache_size,
                            spm_size,
                            read_ports,
                            write_ports,
                            read_bus_width,
                            write_bus_width,
                            cache_ports,
                            local_ports,
                            (_SPM.leakage.power.readOp.leakage+_SPM.leakage.power.writeOp.leakage)*leak,
                            _SPM.dyn_read.power.readOp.dynamic*exponential,
                            _SPM.dyn_write.power.writeOp.dynamic*exponential,
                            _SPM.leakage.area,
                            (_SPM.opt.power.readOp.leakage+_SPM.opt.power.writeOp.leakage)*leak,
                            _SPM.opt.area,
                            (_Cache.leakage.power.readOp.leakage+_Cache.leakage.power.writeOp.leakage)*leak,
                            _Cache.dyn_read.power.readOp.dynamic*exponential,
                            _Cache.dyn_write.power.writeOp.dynamic*exponential,
                            _Cache.leakage.area,
                            _MaxFU.counter_units, 
                            _RunningAverageOccupancy.counter_units,
                            _MaxFU.int_adder_units, 
                            _RunningAverageOccupancy.int_adder_units,
                            _MaxFU.int_multiply_units, 
                            _RunningAverageOccupancy.int_multiply_units,
                            _MaxFU.int_shifter_units, 
                            _RunningAverageOccupancy.int_shifter_units,
                            _MaxFU.int_bit_units, 
                            _RunningAverageOccupancy.int_bit_units,
                            _MaxFU.fp_sp_adder, 
                            _RunningAverageOccupancy.fp_sp_adder,
                            _MaxFU.fp_dp_adder, 
                            _RunningAverageOccupancy.fp_dp_adder,
                            _MaxFU.fp_sp_multiply, 
                            _RunningAverageOccupancy.fp_sp_multiply,
                            _MaxFU.fp_dp_multiply, 
                            _RunningAverageOccupancy.fp_dp_multiply,
                            _MaxFU.compare, 
                            _RunningAverageOccupancy.compare,
                            _MaxFU.gep, 
                            _RunningAverageOccupancy.gep,
                            _MaxFU.conversion, 
                            _RunningAverageOccupancy.conversion,
                            _MaxParsed.counter_units,
                            _MaxParsed.int_adder_units,
                            _MaxParsed.int_multiply_units,
                            _MaxParsed.int_shifter_units,
                            _MaxParsed.int_bit_units,
                            _MaxParsed.fp_sp_adder,
                            _MaxParsed.fp_dp_adder,
                            _MaxParsed.fp_sp_multiply,
                            _MaxParsed.fp_dp_multiply,
                            _MaxParsed.compare,
                            _MaxParsed.gep,
                            _MaxParsed.conversion,
                            _MaxParsed.other,
                            regList->size(),
                            regList->count(),
                            regList->average()/((double)cycle),
                            regList->avgSize()/(regList->average()),
                            pwrUtil->regUsage.reads,
                            pwrUtil->regUsage.writes,
                            memory_loads,
                            memory_stores,
                            dma_loads,
                            dma_stores,
                            pwrUtil->finalPwr.leakage_power,
                            pwrUtil->totalPwr.dynamic_energy*runtime,
                            (pwrUtil->finalPwr.leakage_power) + (pwrUtil->totalPwr.dynamic_energy)*runtime,
                            pwrUtil->totalPwr.reg_leakage_power,
                            pwrUtil->totalPwr.reg_dynamic_energy*runtime,
                            pwrUtil->totalPwr.reg_leakage_power + pwrUtil->totalPwr.reg_dynamic_energy*runtime,
                            (pwrUtil->finalPwr.leakage_power) + (pwrUtil->totalPwr.dynamic_energy)*runtime + ((pwrUtil->totalPwr.reg_leakage_power) + (pwrUtil->totalPwr.reg_dynamic_energy)*runtime),
                            pwrUtil->totalPwr.area,
                            pwrUtil->totalPwr.reg_area,
                            pwrUtil->totalPwr.area + pwrUtil->totalPwr.reg_area);

    results->unitCorrections();
    results->print();
    //results->simpleStats();
    //if (occupancy_tracking) printOccupancyList();
    //regList->printRegNames();
    delete results;
}


void
LLVMInterface::printOccupancyList() {
    std::cout << std::endl;
    for (auto it = _CycleOccList->begin(); it != _CycleOccList->end(); ++it) {
        std::cout << (*it).counter_units << ", ";
        std::cout << (*it).int_adder_units << ", ";
        std::cout << (*it).int_multiply_units << ", ";
        std::cout << (*it).int_shifter_units << ", ";
        std::cout << (*it).int_bit_units << ", ";
        std::cout << (*it).fp_sp_adder << ", ";
        std::cout << (*it).fp_dp_adder << ", ";
        std::cout << (*it).fp_sp_multiply << ", ";
        std::cout << (*it).fp_dp_multiply << ", ";
        std::cout << (*it).compare << ", ";
        std::cout << (*it).gep << ", ";
        std::cout << (*it).conversion << ", ";
        std::cout << (*it).other;
        std::cout << std::endl;
    }
    std::cout << std::endl;
}


void
LLVMInterface::dumpQueues() {
    std::cout << "*********************************************************\n"
              << "Compute Queue\n"
              << "*********************************************************\n";
    for (auto compute : computeQueue) {
        std::cout << compute->_LLVMLine << std::endl;
    }
    std::cout << "*********************************************************\n"
              << "Read Queue\n"
              << "*********************************************************\n";
    for (auto read : readQueue) {
        std::cout << read->_LLVMLine << std::endl;
    }
    std::cout << "*********************************************************\n"
              << "Write Queue\n"
              << "*********************************************************\n";
    for (auto write : writeQueue) {
        std::cout << write->_LLVMLine << std::endl;
    }
    std::cout << "*********************************************************\n"
              << "Reservation Queue\n"
              << "*********************************************************\n";
    for (auto reserved : reservation) {
        std::cout << reserved->_LLVMLine << std::endl;
    }
    std::cout << "*********************************************************\n"
              << "End of queue dump\n"
              << "*********************************************************\n";
}


void
LLVMInterface::initFU() {
    maxFU(_FunctionalUnits);
    _FunctionalUnits.counter_units = 0;
    _FunctionalUnits.int_adder_units = 0;
    _FunctionalUnits.int_multiply_units = 0;
    _FunctionalUnits.int_shifter_units = 0;
    _FunctionalUnits.int_bit_units = 0;
    _FunctionalUnits.fp_sp_adder = 0;
    _FunctionalUnits.fp_dp_adder = 0;
    _FunctionalUnits.fp_sp_multiply = 0;
    _FunctionalUnits.fp_dp_multiply = 0;
    _FunctionalUnits.compare = 0;
    _FunctionalUnits.gep = 0;
    _FunctionalUnits.conversion = 0;
    _FunctionalUnits.other = 0;
    _MaxParsed.counter_units = 0;
    _MaxParsed.int_adder_units = 0;
    _MaxParsed.int_multiply_units = 0;
    _MaxParsed.int_shifter_units = 0;
    _MaxParsed.int_bit_units = 0;
    _MaxParsed.fp_sp_adder = 0;
    _MaxParsed.fp_dp_adder = 0;
    _MaxParsed.fp_sp_multiply = 0;
    _MaxParsed.fp_dp_multiply = 0;
    _MaxParsed.compare = 0;
    _MaxParsed.gep = 0;
    _MaxParsed.conversion = 0;
    _MaxParsed.other = 0;
    _MaxFU.counter_units = 0;
    _MaxFU.int_adder_units = 0;
    _MaxFU.int_multiply_units = 0;
    _MaxFU.int_shifter_units = 0;
    _MaxFU.int_bit_units = 0;
    _MaxFU.fp_sp_adder = 0;
    _MaxFU.fp_dp_adder = 0;
    _MaxFU.fp_sp_multiply = 0;
    _MaxFU.fp_dp_multiply = 0;
    _MaxFU.compare = 0;
    _MaxFU.gep = 0;
    _MaxFU.conversion = 0;
    _MaxFU.other = 0;
    _AverageOccupancy.counter_units = 0;
    _AverageOccupancy.int_adder_units = 0;
    _AverageOccupancy.int_multiply_units = 0;
    _AverageOccupancy.int_shifter_units = 0;
    _AverageOccupancy.int_bit_units = 0;
    _AverageOccupancy.fp_sp_adder = 0;
    _AverageOccupancy.fp_dp_adder = 0;
    _AverageOccupancy.fp_sp_multiply = 0;
    _AverageOccupancy.fp_dp_multiply = 0;
    _AverageOccupancy.compare = 0;
    _AverageOccupancy.gep = 0;
    _AverageOccupancy.conversion = 0;
    _AverageOccupancy.other = 0;
    _RunningAverageOccupancy.counter_units = 0;
    _RunningAverageOccupancy.int_adder_units = 0;
    _RunningAverageOccupancy.int_multiply_units = 0;
    _RunningAverageOccupancy.int_shifter_units = 0;
    _RunningAverageOccupancy.int_bit_units = 0;
    _RunningAverageOccupancy.fp_sp_adder = 0;
    _RunningAverageOccupancy.fp_dp_adder = 0;
    _RunningAverageOccupancy.fp_sp_multiply = 0;
    _RunningAverageOccupancy.fp_dp_multiply = 0;
    _RunningAverageOccupancy.compare = 0;
    _RunningAverageOccupancy.gep = 0;
    _RunningAverageOccupancy.conversion = 0;
    _RunningAverageOccupancy.other = 0;
}


void
LLVMInterface::clearFU() {
    maxFU(_FunctionalUnits);
    Occupancy(_FunctionalUnits);
    _FunctionalUnits.counter_units = 0;
    _FunctionalUnits.int_adder_units = 0;
    _FunctionalUnits.int_multiply_units = 0;
    _FunctionalUnits.int_shifter_units = 0;
    _FunctionalUnits.int_bit_units = 0;
    _FunctionalUnits.fp_sp_adder = 0;
    _FunctionalUnits.fp_dp_adder = 0;
    _FunctionalUnits.fp_sp_multiply = 0;
    _FunctionalUnits.fp_dp_multiply = 0;
    _FunctionalUnits.compare = 0;
    _FunctionalUnits.gep = 0;
    _FunctionalUnits.conversion = 0;
    _FunctionalUnits.other = 0;
    _FunctionalUnits.fpDivision = 0;
}

void
LLVMInterface::Occupancy(FunctionalUnits FU) {
    _AverageOccupancy.counter_units += FU.counter_units;
    _AverageOccupancy.int_adder_units += FU.int_adder_units;
    _AverageOccupancy.int_multiply_units += FU.int_multiply_units;
    _AverageOccupancy.int_shifter_units += FU.int_shifter_units;
    _AverageOccupancy.int_bit_units += FU.int_bit_units;
    _AverageOccupancy.fp_sp_adder += FU.fp_sp_adder;
    _AverageOccupancy.fp_dp_adder += FU.fp_dp_adder;
    _AverageOccupancy.fp_sp_multiply += FU.fp_sp_multiply;
    _AverageOccupancy.fp_dp_multiply += FU.fp_dp_multiply;
    _AverageOccupancy.compare += FU.compare;
    _AverageOccupancy.gep += FU.gep;
    _AverageOccupancy.conversion += FU.conversion;
    _AverageOccupancy.other += FU.other;

    _CycleOccupancy.counter_units = FU.counter_units;
    _CycleOccupancy.int_adder_units = FU.int_adder_units;
    _CycleOccupancy.int_multiply_units = FU.int_multiply_units;
    _CycleOccupancy.int_shifter_units = FU.int_shifter_units;
    _CycleOccupancy.int_bit_units = FU.int_bit_units;
    _CycleOccupancy.fp_sp_adder = FU.fp_sp_adder;
    _CycleOccupancy.fp_dp_adder = FU.fp_dp_adder;
    _CycleOccupancy.fp_sp_multiply = FU.fp_sp_multiply;
    _CycleOccupancy.fp_dp_multiply = FU.fp_dp_multiply;
    _CycleOccupancy.compare = FU.compare;
    _CycleOccupancy.gep = FU.gep;
    _CycleOccupancy.conversion = FU.conversion;
    _CycleOccupancy.other = FU.other;
    if (occupancy_tracking) {
        _CycleOccList->push_back(_CycleOccupancy);
    }
}


void
LLVMInterface::AverageOccupancy() {
    if (_AverageOccupancy.counter_units != 0) _RunningAverageOccupancy.counter_units = (_AverageOccupancy.counter_units/_MaxFU.counter_units)/cycle;
    if (_AverageOccupancy.int_adder_units != 0) _RunningAverageOccupancy.int_adder_units = (_AverageOccupancy.int_adder_units/_MaxFU.int_adder_units)/cycle;
    if (_AverageOccupancy.int_multiply_units != 0) _RunningAverageOccupancy.int_multiply_units = (_AverageOccupancy.int_multiply_units/_MaxFU.int_multiply_units)/cycle;
    if (_AverageOccupancy.int_shifter_units != 0) _RunningAverageOccupancy.int_shifter_units = (_AverageOccupancy.int_shifter_units/_MaxFU.int_shifter_units)/cycle;
    if (_AverageOccupancy.int_bit_units != 0) _RunningAverageOccupancy.int_bit_units = (_AverageOccupancy.int_bit_units/_MaxFU.int_bit_units)/cycle;
    if (_AverageOccupancy.fp_sp_adder != 0) _RunningAverageOccupancy.fp_sp_adder = (_AverageOccupancy.fp_sp_adder/_MaxFU.fp_sp_adder)/cycle;
    if (_AverageOccupancy.fp_dp_adder != 0) _RunningAverageOccupancy.fp_dp_adder = (_AverageOccupancy.fp_dp_adder/_MaxFU.fp_dp_adder)/cycle;
    if (_AverageOccupancy.fp_sp_multiply != 0) _RunningAverageOccupancy.fp_sp_multiply = (_AverageOccupancy.fp_sp_multiply/_MaxFU.fp_sp_multiply)/cycle;
    if (_AverageOccupancy.fp_dp_multiply != 0) _RunningAverageOccupancy.fp_dp_multiply = (_AverageOccupancy.fp_dp_multiply/_MaxFU.fp_dp_multiply)/cycle;
    if (_AverageOccupancy.compare != 0) _RunningAverageOccupancy.compare = (_AverageOccupancy.compare/_MaxFU.compare)/cycle;
    if (_AverageOccupancy.gep != 0) _RunningAverageOccupancy.gep = (_AverageOccupancy.gep/_MaxFU.gep)/cycle;
    if (_AverageOccupancy.conversion != 0) _RunningAverageOccupancy.conversion = (_AverageOccupancy.conversion/_MaxFU.conversion)/cycle;
    if (_AverageOccupancy.other != 0) _RunningAverageOccupancy.other = (_AverageOccupancy.other/_MaxFU.other)/cycle;
}

void
LLVMInterface::updateFU(int8_t FU) {
    switch(FU) {
        case COUNTER: _FunctionalUnits.counter_units++; break;
        case INTADDER: _FunctionalUnits.int_adder_units++; break;
        case INTMULTI: _FunctionalUnits.int_multiply_units++; break;
        case INTSHIFTER:  _FunctionalUnits.int_shifter_units++; break;
        case INTBITWISE: _FunctionalUnits.int_bit_units++; break;
        case FPSPADDER: _FunctionalUnits.fp_sp_adder++; break;
        case FPDPADDER: _FunctionalUnits.fp_dp_adder++; break;
        case FPSPMULTI: _FunctionalUnits.fp_sp_multiply++; break;
        case FPDPMULTI: _FunctionalUnits.fp_dp_multiply++; break;
        case COMPARE: _FunctionalUnits.compare++; break;
        case GETELEMENTPTR: _FunctionalUnits.gep++; break;
        case CONVERSION: _FunctionalUnits.conversion++; break;
        default: _FunctionalUnits.other++;
    }
}

void
LLVMInterface::updateParsedFU(int8_t FU) {
    switch(FU) {
        case COUNTER: _MaxParsed.counter_units++; break;
        case INTADDER: _MaxParsed.int_adder_units++; break;
        case INTMULTI: _MaxParsed.int_multiply_units++; break;
        case INTSHIFTER:  _MaxParsed.int_shifter_units++; break;
        case INTBITWISE: _MaxParsed.int_bit_units++; break;
        case FPSPADDER: _MaxParsed.fp_sp_adder++; break;
        case FPDPADDER: _MaxParsed.fp_dp_adder++; break;
        case FPSPMULTI: _MaxParsed.fp_sp_multiply++; break;
        case FPDPMULTI: _MaxParsed.fp_dp_multiply++; break;
        case COMPARE: _MaxParsed.compare++; break;
        case GETELEMENTPTR: _MaxParsed.gep++; break;
        case CONVERSION: _MaxParsed.conversion++; break;
        default: _MaxParsed.other++; break;
    }
}

void
LLVMInterface::maxFU(FunctionalUnits FU) {
    if(FU.counter_units > _MaxFU.counter_units) _MaxFU.counter_units = FU.counter_units;
    if(FU.int_adder_units > _MaxFU.int_adder_units) _MaxFU.int_adder_units = FU.int_adder_units;
    if(FU.int_multiply_units > _MaxFU.int_multiply_units) _MaxFU.int_multiply_units = FU.int_multiply_units;
    if(FU.int_shifter_units > _MaxFU.int_shifter_units) _MaxFU.int_shifter_units = FU.int_shifter_units;
    if(FU.int_bit_units > _MaxFU.int_bit_units) _MaxFU.int_bit_units = FU.int_bit_units;
    if(FU.fp_sp_adder > _MaxFU.fp_sp_adder) _MaxFU.fp_sp_adder = FU.fp_sp_adder;
    if(FU.fp_dp_adder > _MaxFU.fp_dp_adder) _MaxFU.fp_dp_adder = FU.fp_dp_adder;
    if(FU.fp_sp_multiply > _MaxFU.fp_sp_multiply) _MaxFU.fp_sp_multiply = FU.fp_sp_multiply;
    if(FU.fp_dp_multiply > _MaxFU.fp_dp_multiply) _MaxFU.fp_dp_multiply = FU.fp_dp_multiply;
    if(FU.compare > _MaxFU.compare) _MaxFU.compare = FU.compare;
    if(FU.gep > _MaxFU.gep) _MaxFU.gep = FU.gep;
    if(FU.conversion > _MaxFU.conversion) _MaxFU.conversion = FU.conversion;
    if(FU.other > _MaxFU.other) _MaxFU.other = FU.other;
}

bool
LLVMInterface::limitedFU(int8_t FU) {
    bool available = false;
    switch(FU) {
        case COUNTER: { 
            if(counter_units == -1) {
                _FunctionalUnits.counter_units++; 
                available = true;
            } else if(_FunctionalUnits.counter_units < counter_units) {
                _FunctionalUnits.counter_units++; 
                available = true; 
            }
            break;
        }
        case INTADDER: {
            if(int_adder_units == -1) {
                _FunctionalUnits.int_adder_units++; 
                available = true;
            } else if(_FunctionalUnits.int_adder_units < int_adder_units) {
                _FunctionalUnits.int_adder_units++; 
                available = true; 
            }
            break;
        }
        case INTMULTI: {
            if(int_multiply_units == -1) {
                _FunctionalUnits.int_multiply_units++; 
                available = true;
            } else if(_FunctionalUnits.int_multiply_units < int_multiply_units) {
                _FunctionalUnits.int_multiply_units++; 
                available = true; 
            }
            break;
        }
        case INTSHIFTER: {
            if(int_shifter_units == -1) {
                _FunctionalUnits.int_shifter_units++; 
                available = true;
            } else if(_FunctionalUnits.int_shifter_units < int_shifter_units) {
                _FunctionalUnits.int_shifter_units++; 
                available = true; 
            }
            break;
        }
        case INTBITWISE: {
            if(int_bit_units == -1) {
                _FunctionalUnits.int_bit_units++; 
                available = true;
            } else if(_FunctionalUnits.int_bit_units < int_bit_units) {
                _FunctionalUnits.int_bit_units++; 
                available = true; 
            }
            break;
        }
        case FPSPADDER: {
            if(fp_sp_adder == -1) {
                _FunctionalUnits.fp_sp_adder++; 
                available = true;
            } else if(_FunctionalUnits.fp_sp_adder < fp_sp_adder) {
                _FunctionalUnits.fp_sp_adder++; 
                available = true; 
            }
            break;
        }
        case FPDPADDER: {
            if(fp_dp_adder == -1) {
                _FunctionalUnits.fp_dp_adder++; 
                available = true;
            } else if(_FunctionalUnits.fp_dp_adder < fp_dp_adder) {
                _FunctionalUnits.fp_dp_adder++; 
                available = true; 
            }
            break;
        }
        case FPSPMULTI: {
            if(fp_sp_multiply == -1) {
                _FunctionalUnits.fp_sp_multiply++; 
                available = true;
            } else if(_FunctionalUnits.fp_sp_multiply < fp_sp_multiply) {
                _FunctionalUnits.fp_sp_multiply++; 
                available = true; 
            }
            break;
        }
        case FPDPMULTI: {
            if(fp_dp_multiply == -1) {
                _FunctionalUnits.fp_dp_multiply++; 
                available = true;
            } else if(_FunctionalUnits.fp_dp_multiply < fp_dp_multiply) {
                _FunctionalUnits.fp_dp_multiply++; 
                available = true; 
            }
            break;
        }
        case COMPARE: {
            if(compare == -1) {
                _FunctionalUnits.compare++; 
                available = true;
            } else if(_FunctionalUnits.compare < compare) {
                _FunctionalUnits.compare++; 
                available = true; 
            }
            break;
        }
        case GETELEMENTPTR: {
            if(gep == -1) {
                _FunctionalUnits.gep++; 
                available = true;
            } else if(_FunctionalUnits.gep < gep) {
                _FunctionalUnits.gep++; 
                available = true; 
            }
            break;
        }
        case CONVERSION: {
            if(conversion == -1) {
                _FunctionalUnits.conversion++; 
                available = true;
            } else if(_FunctionalUnits.conversion < conversion) {
                _FunctionalUnits.conversion++; 
                available = true; 
            }
            break;
        }
        default: {
            available = true;
            break;
        }
    }
    return available;
}
