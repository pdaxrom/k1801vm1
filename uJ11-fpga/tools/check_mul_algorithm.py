from random import Random
r=Random(25)
def signed(x):return x-65536 if x&32768 else x
def multiply(x,y):
 high=low=0;bits=y;ext=65535 if x&32768 else 0
 for _ in range(16):
  high=((high<<1)|(low>>15))&65535;low=(low<<1)&65535
  bit=bits>>15;bits=(bits<<1)&65535
  if bit:
   total=low+x;low=total&65535;high=(high+ext+(total>>16))&65535
 if y&32768:high=(high-x)&65535
 carry=high!=(65535 if low&32768 else 0)
 flags=(8 if high&32768 else 0)|(4 if not high|low else 0)|carry
 return (high<<16)|low,flags
edges=[0,1,2,3,0x7fff,0x8000,0x8001,0xffff,0xfffe,0x4000,0xc000,0x5555,0xaaaa]
pairs=[(x,y) for x in edges for y in range(65536)]+[(r.randrange(65536),r.randrange(65536)) for _ in range(10000)]
for x,y in pairs:
 p=signed(x)*signed(y);expected=((p&0xffffffff),(8 if p<0 else 0)|(4 if p==0 else 0)|(p < -32768 or p > 32767))
 assert multiply(x,y)==expected,(x,y,multiply(x,y),expected)
print('PASS candidate serial multiply:',len(pairs),'independent product/NZVC comparisons')
