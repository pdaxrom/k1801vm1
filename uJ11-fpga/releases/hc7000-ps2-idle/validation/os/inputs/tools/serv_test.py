"""ELF-layout arguments for the real SERV memory-write guard in RTL tests."""
GUARD='tests/models/serv_memory_guard.v'
def memory_args(record):
    return [f'+IOP_{name.upper()}={value:x}' for name,value in record['iop']['memory'].items()]
