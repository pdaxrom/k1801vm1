"""SD menu envelope: descriptor at LBA 2, up to 16 payload sectors at LBA 3."""
import binascii
import struct
MAGIC=b'UJ11MENU'
ENTRY=0o100000
MAX_BLOCKS=16
MIN_RESERVED=3+MAX_BLOCKS

def crc16(data):return binascii.crc_hqx(data,0)

def pack(payload):
    if not payload or len(payload)>512*MAX_BLOCKS or len(payload)%2:
        raise ValueError('menu must be a nonempty, even-sized binary of at most 8192 bytes')
    blocks=(len(payload)+511)//512
    data=payload.ljust(blocks*512,b'\0')
    header=struct.pack('<8sHHHH',MAGIC,blocks,ENTRY,crc16(data),1).ljust(510,b'\0')
    return header+crc16(header).to_bytes(2,'big')+data

def unpack(image):
    if len(image)<1024 or len(image)%512:
        raise ValueError('short/misaligned menu image')
    magic,blocks,entry,crc,version=struct.unpack_from('<8sHHHH',image)
    if magic!=MAGIC or version!=1 or entry!=ENTRY or not 1<=blocks<=MAX_BLOCKS or len(image)!=(blocks+1)*512:
        raise ValueError('invalid menu descriptor')
    if any(image[16:510]) or crc16(image[:512]):
        raise ValueError('menu descriptor CRC/reserved bytes')
    if crc16(image[512:])!=crc:
        raise ValueError('menu payload CRC16 mismatch')
    return dict(blocks=blocks,entry_octal=f'{entry:06o}',crc16=f'{crc:04x}')

def read(f):
    f.seek(2*512);header=f.read(512)
    if len(header)!=512:raise ValueError('short menu header')
    blocks=int.from_bytes(header[8:10],'little')
    if not 1<=blocks<=MAX_BLOCKS:raise ValueError('invalid menu sector count')
    return unpack(header+f.read(blocks*512))
