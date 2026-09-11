#!/usr/bin/env python3
"""Check actual EDIF net drivers and identify the CP52 FRAM carry instances."""
import hashlib
import json
import re
from board_common import ROOT


def parse(text):
    stack=[[]]
    for token in re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^()\s]+',text):
        if token=='(':stack.append([])
        elif token==')':
            node=stack.pop();stack[-1].append(node)
        else:stack[-1].append(token)
    assert len(stack)==1 and len(stack[0])==1
    return stack[0][0]


def children(node,key):return [n for n in node if isinstance(n,list) and n[0]==key]
def child(node,key):return next(n for n in node if isinstance(n,list) and n[0]==key)
def ident(n):return ident(n[1]) if isinstance(n,list) else n


def audit(path):
    tree=parse(path.read_text());cells={}
    for library in children(tree,'library')+children(tree,'external'):
        for cell in children(library,'cell'):
            view=child(cell,'view');interface=child(view,'interface')
            ports={ident(p[1]):child(p,'direction')[1] for p in children(interface,'port')}
            cells[(ident(library[1]),ident(cell[1]))]=(cell,view,ports)
    net_count=0;bad=[];bidirectional=[];fram_carry=[];fram_net_count=0
    for key,(cell,view,ports) in cells.items():
        contents=children(view,'contents')
        if not contents:continue
        contents=contents[0];instances={}
        for inst in children(contents,'instance'):
            ref=child(child(inst,'viewRef'),'cellRef')
            library=children(ref,'libraryRef')
            target=(ident(library[0][1]) if library else key[0],ident(ref[1]))
            assert target in cells,target
            instances[ident(inst[1])]=target
        fram=key[1].startswith('uj11_board_fram')
        if fram:fram_carry += [n for n,c in instances.items() if c[1]=='CCU2D']
        for net in children(contents,'net'):
            net_count+=1;fram_net_count+=int(fram);drivers=[];inouts=[]
            for port in children(child(net,'joined'),'portRef'):
                instance=children(port,'instanceRef');p=ident(port[1])
                if instance:
                    name=ident(instance[0][1]);direction=cells[instances[name]][2][p]
                    if direction=='OUTPUT':drivers.append(name+'.'+p)
                    if direction=='INOUT':inouts.append(name+'.'+p)
                else:
                    if ports[p]=='INPUT':drivers.append('input.'+p)
                    if ports[p]=='INOUT':inouts.append('inout.'+p)
            if len(drivers)>1:bad.append(dict(cell=key,net=ident(net[1]),drivers=drivers,inouts=inouts))
            if inouts:bidirectional.append(dict(cell=key,net=ident(net[1]),drivers=drivers,inouts=inouts))
    assert not bad,bad[:10]
    return dict(edif_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),nets_checked=net_count,
        strong_multiple_driver_nets=bad,bidirectional_nets=bidirectional,fram_nets_checked=fram_net_count,fram_ccu2d=fram_carry,
        scope='Each EDIF cell net; direction-defined strong drivers; bidirectional nets recorded separately, not a physical contention proof')


def main():
    out=ROOT/'build/cp53-netlist';rows={}
    for gate in ('cp52a','cp52b'):
        rows[gate]=audit(out/(gate+'_impl1.edi'))
        print(gate,rows[gate]['nets_checked'],'nets, no strong multiple drivers; FRAM CCU2D',len(rows[gate]['fram_ccu2d']))
    assert len(rows['cp52a']['fram_ccu2d'])==0
    assert len(rows['cp52b']['fram_ccu2d'])==13
    # Confirm that this is a driver check, not just a successful EDIF parse.
    source=(out/'cp52b_impl1.edi').read_text()
    pin='(portRef Q (instanceRef next_word_0))';assert source.count(pin)==1
    bad=out/'bad-driver.edi';bad.write_text(source.replace(pin,pin+'\n (portRef Q (instanceRef next_word_1))'))
    try:audit(bad)
    except AssertionError as error:
        assert 'next_word_1.Q' in str(error),error
    else:raise AssertionError('injected duplicate driver escaped detection')
    rows['negative_driver_control_rejected']=True
    rows['script_sha256']=hashlib.sha256((ROOT/'tools/audit_edif_cp53.py').read_bytes()).hexdigest()
    (out/'audit.json').write_text(json.dumps(rows,indent=2)+'\n')


if __name__=='__main__':main()
