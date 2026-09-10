#!/usr/bin/env python3
"""Check final EDIF port directions; do not waive intermediate BN161 warnings."""
import argparse
import copy
import hashlib
import json
import re
from pathlib import Path


def parse(text):
    stack = [[]]
    for token in re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+', text):
        if token == '(':
            node = []; stack[-1].append(node); stack.append(node)
        elif token == ')': stack.pop()
        else: stack[-1].append(token)
    assert len(stack) == 1 and len(stack[0]) == 1
    return stack[0][0]


def children(node, key): return [x for x in node if isinstance(x, list) and x[0] == key]
def child(node, key): return children(node, key)[0]
def ident(value): return value if isinstance(value, str) else ident(value[1])


def audit(path):
    tree = parse(path.read_text()); cells = {}
    for lib in children(tree, 'library')+children(tree, 'external'):
        for cell in children(lib, 'cell'): cells[(ident(lib[1]), ident(cell[1]))] = cell
    ports = {}
    for key, cell in cells.items():
        view = child(cell, 'view'); interface = child(view, 'interface')
        ports[key] = {ident(p[1]): child(p, 'direction')[1] for p in children(interface, 'port')}
    conflicts = []; floating = []; masked_carry = []; nets = 0; bidirectional = 0
    for key, cell in cells.items():
        contents = children(child(cell, 'view'), 'contents')
        if not contents: continue
        instances = {}; instance_nodes = {}
        for instance in children(contents[0], 'instance'):
            ref = child(child(instance, 'viewRef'), 'cellRef')
            library = children(ref, 'libraryRef')
            instances[ident(instance[1])] = (ident(library[0][1]) if library else key[0], ident(ref[1]))
            instance_nodes[ident(instance[1])] = instance
        pin_nets = {}; constants = {}
        for net in children(contents[0], 'net'):
            for p in children(child(net, 'joined'), 'portRef'):
                refs = children(p, 'instanceRef')
                if refs:
                    name = ident(refs[0][1]); pin_nets[name, ident(p[1])] = net
                    if instances[name][1] in ('VLO', 'VHI'):
                        constants[ident(net[1])] = int(instances[name][1] == 'VHI')

        def ignored_cin(p):
            refs = children(p, 'instanceRef')
            if not refs or ident(p[1]) != 'CIN': return False
            name = ident(refs[0][1])
            if instances[name][1] != 'CCU2D': return False
            props = {ident(p[1]): child(p, 'string')[1].strip('"') for p in children(instance_nodes[name], 'property')}
            if 'INIT0' not in props or props.get('INJECT1_0') not in ('YES','NO'): return False
            init = int(props['INIT0'], 0)
            values = [constants.get(ident(pin_nets[name, pin][1])) for pin in ('A0','B0','C0','D0')]
            # Lattice CCU2D: cout_0=(~prop_0 & gen_0)|(prop_0 & CIN).
            # Prove prop_0=0 for every assignment of nonconstant LUT inputs.
            for bits in range(16):
                if all(v is None or v == ((bits >> i) & 1) for i, v in enumerate(values)):
                    if (init >> bits) & 1: return False
            s0 = pin_nets.get((name, 'S0'))
            # sum_0 can depend on CIN with INJECT1_0=NO. It must be unused.
            return props['INJECT1_0'] == 'YES' or s0 is None or len(children(child(s0, 'joined'), 'portRef')) == 1
        for net in children(contents[0], 'net'):
            drivers = []; inout = []; nets += 1
            for p in children(child(net, 'joined'), 'portRef'):
                instance = children(p, 'instanceRef'); pin = ident(p[1])
                if instance:
                    name = ident(instance[0][1]); direction = ports[instances[name]][pin]
                    drives = direction == 'OUTPUT'; endpoint = name+'.'+str(pin)
                else:
                    direction = ports[key][pin]; drives = direction == 'INPUT'; endpoint = 'boundary.'+str(pin)
                if drives: drivers.append(endpoint)
                if direction == 'INOUT': inout.append(endpoint)
            name = '/'.join(key)+'/'+str(ident(net[1]))
            if len(drivers) > 1: conflicts.append(dict(net=name, drivers=drivers))
            if not drivers and not inout:
                refs = children(child(net, 'joined'), 'portRef')
                if refs and all(ignored_cin(p) for p in refs): masked_carry.append(name)
                else: floating.append(name)
            if inout: bidirectional += 1
    result = dict(edif_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), cells=len(cells),
                  nets=nets, bidirectional_nets=bidirectional, multiple_drivers=conflicts, floating=floating,
                  proven_unused_carry_inputs=masked_carry)
    return result


def negative_controls(path, out):
    """Mutate real final-netlist connections, not a separate toy circuit."""
    pristine = parse(path.read_text()); out.mkdir(parents=True, exist_ok=True)
    results = []
    def encode(node):
        return '('+' '.join(encode(x) for x in node)+')' if isinstance(node, list) else node
    for mutation in ('second-driver', 'open-input', 'observable-cin'):
        tree = copy.deepcopy(pristine); changed = False
        for lib in children(tree, 'library'):
            for cell in children(lib, 'cell'):
                view = child(cell, 'view'); contents = children(view, 'contents')
                if not contents or changed: continue
                boundary = [ident(p[1]) for p in children(child(view, 'interface'), 'port')
                            if child(p, 'direction')[1] == 'INPUT']
                if mutation == 'observable-cin':
                    for inst in children(contents[0], 'instance'):
                        ref = child(child(inst, 'viewRef'), 'cellRef')
                        if ident(ref[1]) != 'CCU2D': continue
                        inst_name = ident(inst[1])
                        nets = children(contents[0], 'net')
                        if not any(children(child(n, 'joined'), 'portRef') ==
                                   [['portRef', 'CIN', ['instanceRef', inst_name]]] for n in nets): continue
                        for prop in children(inst, 'property'):
                            if ident(prop[1]) == 'INIT0':
                                child(prop, 'string')[1] = '"0xffff"'; changed = True; break
                        if changed: break
                else:
                    for net in children(contents[0], 'net'):
                        joined = child(net, 'joined'); refs = children(joined, 'portRef')
                        drivers = [p for p in refs if not children(p, 'instanceRef') and ident(p[1]) in boundary]
                        if len(drivers) != 1 or len(refs) < 2 or len(boundary) < 2: continue
                        if mutation == 'open-input': joined.remove(drivers[0])
                        else:
                            second = next(p for p in boundary if p != ident(drivers[0][1]))
                            joined.append(['portRef', second])
                        changed = True; break
        assert changed, mutation
        destination = out/(mutation+'.edi'); destination.write_text(encode(tree)+'\n')
        result = audit(destination)
        expected = 'multiple_drivers' if mutation == 'second-driver' else 'floating'
        assert result[expected], (mutation, result)
        results.append(dict(mutation=mutation, detected=expected, result=result))
    return results


def main():
    p = argparse.ArgumentParser(description=__doc__); p.add_argument('edif', type=Path)
    p.add_argument('--output', type=Path)
    p.add_argument('--negative-controls', type=Path); args = p.parse_args()
    result = audit(args.edif)
    if args.negative_controls: result['negative_controls'] = negative_controls(args.edif, args.negative_controls)
    if args.output: args.output.write_text(json.dumps(result, indent=2)+'\n')
    assert not result['multiple_drivers'], result['multiple_drivers']
    assert not result['floating'], result['floating']
    print('PASS final EDIF:', result['nets'], 'nets, no multiple drivers or unexplained floating nets;',
          len(result['proven_unused_carry_inputs']), 'CIN inputs proved unobservable')


if __name__ == '__main__': main()
