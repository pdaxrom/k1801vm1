"""Small PDP-11 program encoder for directed RTL fixtures."""
class Program:
    def __init__(self,base=0o20000): self.base=base;self.words=[]
    @property
    def pc(self):return self.base+2*len(self.words)
    def emit(self,*words):self.words.extend(words);return self
    def mov(self,reg,value):return self.emit(0o012700+reg,value)
    def store(self,reg,address):return self.emit(0o010037+(reg<<6),address)
    def load(self,reg,address):return self.emit(0o013700+reg,address)
    def upper(self,address,value):return self.mov(5,address).mov(0,value).emit(0o41)
    def install(self,handler,vector=8):
        self.upper(vector,handler.base).upper(vector+2,0o340)
        for i,w in enumerate(handler.words):self.upper(handler.base+2*i,w)
        return self
