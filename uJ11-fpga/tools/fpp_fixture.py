"""Initialize only external test memory with the current software FPP."""
from board_common import ROOT
from build_fpp import build, OUT
from build_hardware import adapt
from pdp11_program import Program


def prepare():
    fp=build();adapt();s=fp['symbols']
    image=[0]*65536;raw=(OUT/'software/image.bin').read_bytes()
    for i in range(0,len(raw),2):image[32768+(s['INIT']+i)//2]=int.from_bytes(raw[i:i+2],'little')
    boot=Program(0o2400).mov(6,0o3700).mov(0,5).emit(0o42)
    boot.mov(0,0o7000).store(0,0o122).emit(0o4737,s['INIT']).store(0,0o6004)
    for addr,target in ((0o6000,s['FPS']),(0o6002,0o102),(0o6010,s['FEC']),(0o6012,s['FEA'])):
        boot.load(0,addr).store(0,target)
    boot.mov(0,0o1000).store(0,0o100)
    for r in range(7):boot.load(r,0o6100+2*r)
    boot.emit(0o10)
    for i,w in enumerate(boot.words):image[32768+boot.base//2+i]=w
    (OUT/'test-image.mem').write_text(''.join(f'{w:04x}\n' for w in image))
    (OUT/'fpp_symbols.vh').write_text(''.join(f'localparam FP_{n}={s[n]};\n' for n in ('FPS','FEC','FEA','ACS','ENTER','MEMEND','DLOOP','MNORM','MMLOOP','MPINT')))
    return fp
