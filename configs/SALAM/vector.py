import m5
from m5.objects import *
from m5.util import *
import ConfigParser
from HWAccConfig import *

def buildHead(options, system, clstr):
    # Specify the path to the mobilenet accelerator descriptions
    hw_path = options.accpath + "/vector/hw"
    hw_config_path = hw_path + "/configs/head/"
    hw_ir_path = hw_path + "/ir/head/"
    local_low = 0x2F000000
    local_high = 0x2F001ADE
    local_range = AddrRange(local_low, local_high)
    external_range = [AddrRange(0x00000000, local_low-1),
                      AddrRange(local_high+1, 0xFFFFFFFF)]
    clstr._attach_bridges(system, local_range, external_range)
    clstr._connect_caches(system, options, l2coherent=False)
    gic = system.realview.gic

    # Add the top function
    acc = "top"
    config = hw_config_path + acc + ".ini"
    ir = hw_ir_path + acc + ".ll"
    clstr.top = CommInterface(devicename=acc, gic=gic)
    AccConfig(clstr.top, config, ir)
    clstr._connect_hwacc(clstr.top)
    clstr.top.local = clstr.local_bus.slave
    clstr.top.enable_debug_msgs = True

    # Add the Stream DMAs
    addr = local_low + 0x0041
    clstr.stream_dma0 = StreamDma(pio_addr=addr, pio_size=32, gic=gic, max_pending=32)
    clstr.stream_dma0.stream_addr=addr+32
    clstr.stream_dma0.stream_size=8
    clstr.stream_dma0.pio_delay='1ns'
    clstr.stream_dma0.rd_int = 210
    clstr.stream_dma0.wr_int = 211
    clstr._connect_dma(system, clstr.stream_dma0)

    # Add the cluster DMA
    addr = local_low + 0x00069
    clstr.dma = NoncoherentDma(pio_addr=addr, pio_size=21, gic=gic, int_num=95)
    clstr._connect_cluster_dma(system, clstr.dma)

    # Add the Normal Convolution
    acc = "S1"
    config = hw_config_path + acc + ".ini"
    ir = hw_ir_path + acc + ".ll"
    clstr.S1 = CommInterface(devicename=acc, gic=gic)
    AccConfig(clstr.S1, config, ir)
    clstr._connect_hwacc(clstr.S1)
    clstr.S1.stream = clstr.stream_dma0.stream_out
    clstr.S1.enable_debug_msgs = True

    addr = local_low + 0x0081
    spmRange = AddrRange(addr, addr+(160*2*3))
    clstr.S1Buffer = ScratchpadMemory(range=spmRange)
    clstr.S1Buffer.conf_table_reported = False
    clstr.S1Buffer.ready_mode=True
    clstr.S1Buffer.port = clstr.local_bus.master
    for i in range(1):
        clstr.S1.spm = clstr.S1Buffer.spm_ports

    addr = local_low + 0x0774
    clstr.S1Out = StreamBuffer(stream_address=addr, stream_size=1, buffer_size=8)
    clstr.S1.stream = clstr.S1Out.stream_in

    # Add the Depthwise Convolution
    acc = "S2"
    config = hw_config_path + acc + ".ini"
    ir = hw_ir_path + acc + ".ll"
    clstr.S2 = CommInterface(devicename=acc, gic=gic)
    AccConfig(clstr.S2, config, ir)
    clstr._connect_hwacc(clstr.S2)
    clstr.S2.stream = clstr.S1Out.stream_out
    

    addr = local_low + 0x18E5
    clstr.S2Out = StreamBuffer(stream_address=addr, stream_size=1, buffer_size=8)
    clstr.S2.stream = clstr.S2Out.stream_in
    clstr.S2.enable_debug_msgs = True
    
    # Add the Pointwise Convolution
    acc = "S3"
    config = hw_config_path + acc + ".ini"
    ir = hw_ir_path + acc + ".ll"
    clstr.S3 = CommInterface(devicename=acc, gic=gic)
    AccConfig(clstr.S3, config, ir)
    clstr._connect_hwacc(clstr.S3)
    clstr.S3.stream = clstr.S2Out.stream_out
    clstr.S3.stream = clstr.stream_dma0.stream_in
    clstr.S3.enable_debug_msgs = True

def makeHWAcc(options, system):
    system.head = AccCluster()
    buildHead(options, system, system.head)

