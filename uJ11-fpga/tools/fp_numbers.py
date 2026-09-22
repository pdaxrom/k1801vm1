"""Exact F/D conversion helpers for independent test expectations."""
from fractions import Fraction
FRAC=(1<<55)-1
SIGN=1<<63

def value(raw):
    e=(raw>>55)&255
    if not e:return Fraction(0)
    n=(raw&FRAC)|(1<<55)
    shift=e-128-56
    v=Fraction(n*(1<<shift)) if shift>=0 else Fraction(n,1<<(-shift))
    return -v if raw&SIGN else v


def pack(x,d,chop):
    if not x:return 0,0,1
    n,den=abs(x).as_integer_ratio();log=n.bit_length()-den.bit_length()
    if Fraction(2)**log>abs(x):log-=1
    e=log+129;p=56 if d else 24;units=abs(x)*Fraction(2)**(p+128-e)
    q,r=divmod(units.numerator,units.denominator)
    if not chop and 2*r>=units.denominator:q+=1
    if q==1<<p:q>>=1;e+=1
    return (int(x<0)<<63)|((e&255)<<55)|((q<<(56-p))&((1<<55)-1)),e,0
