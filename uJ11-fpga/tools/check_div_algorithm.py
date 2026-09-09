from random import Random

def signed(x,bits):return x-(1<<bits) if x&(1<<(bits-1)) else x

def reference(x,y):
 a=signed(x,32);b=signed(y,16)
 if not b:return None,None,7
 q=abs(a)//abs(b)
 if (a<0)!=(b<0):q=-q
 if q < -32768 or q > 32767:return None,None,2|(8 if q<0 else 0)
 return q&65535,(a-b*q)&65535,(8 if q<0 else 0)|(4 if q==0 else 0)

def divide(x,y):
 if not y:return None,None,7
 neg_a=bool(x&0x80000000);neg_q=neg_a!=bool(y&32768)
 m=(-x if neg_a else x)&0xffffffff;d=(-y if y&32768 else y)&65535
 high=m>>16;low=m&65535
 if high>=d:return None,None,2|(8 if neg_q else 0)
 for _ in range(16):
  high=((high<<1)|(low>>15))&65535;low=(low<<1)&65535
  assert high<2*d
  if high>=d:high-=d;low|=1
 if low>(32768 if neg_q else 32767):return None,None,2|(8 if neg_q else 0)
 if neg_q:low=(-low)&65535
 if neg_a:high=(-high)&65535
 return low,high,(8 if low&32768 else 0)|(4 if low==0 else 0)

def main():
 edges=[0,1,2,0x7fff,0x8000,0xffff,0x10000,0x7fffffff,0x80000000,0x80000001,0xffffffff,0xffff8000,0xffff7fff]
 r=Random(26);count=0
 for x,y in ((x,y) for x in edges for y in range(65536)):
  assert divide(x,y)==reference(x,y),(hex(x),hex(y),divide(x,y),reference(x,y));count+=1
 for _ in range(10000):
  x=r.randrange(1<<32);y=r.randrange(65536)
  assert divide(x,y)==reference(x,y);count+=1
 print('PASS candidate serial divide:',count,'independent quotient/remainder/NZVC comparisons')
if __name__=='__main__':main()
