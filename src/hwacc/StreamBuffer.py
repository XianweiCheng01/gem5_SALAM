from m5.params import *
from m5.proxy import *
from m5.objects.ClockedObject import ClockedObject
# from Device import BasicPioDevice

class StreamBuffer(ClockedObject):
	type = 'StreamBuffer'
	cxx_header = 'hwacc/stream_buffer.hh'
	system = Param.System(Parent.any, "System this devices is part of")
	stream_in = SlavePort("Stream buffer access port")
	stream_out = SlavePort("Stream buffer access port")
	buffer_size = Param.UInt64(256, "Stream buffer depth in bytes")
	stream_address = Param.Addr("Address ")
	stream_size = Param.Addr("Stream buffer width in bytes")
	stream_latency = Param.Latency('1ns', 'Stream W/R latency')
	bandwidth = Param.MemoryBandwidth('12.6GB/s', "Combined read and write bandwidth")