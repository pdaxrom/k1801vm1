"""Generate the ODT instruction-format tables."""
def tables():
    rows=[]
    def add(mask, value, form, name): rows.append((mask,value,form,name))
    for v,n in enumerate(('HALT','WAIT','RTI','BPT','IOT','RESET','RTT','MFPT')):add(0xffff,v,0,n)
    for v,n in [(0o240,'NOP'),(0o241,'CLC'),(0o242,'CLV'),(0o244,'CLZ'),(0o250,'CLN'),
                (0o257,'CCC'),(0o261,'SEC'),(0o262,'SEV'),(0o264,'SEZ'),(0o270,'SEN'),(0o277,'SCC')]:add(0xffff,v,0,n)
    # Unnamed flag combinations are .WORD (do not mislabel operand formats).
    for base,n in [(0o10000,'MOV'),(0o20000,'CMP'),(0o30000,'BIT'),(0o40000,'BIC'),(0o50000,'BIS'),(0o60000,'ADD'),
                   (0o110000,'MOVB'),(0o120000,'CMPB'),(0o130000,'BITB'),(0o140000,'BICB'),(0o150000,'BISB'),(0o160000,'SUB')]:add(0xf000,base,2,n)
    for base,n in [(0o400,'BR'),(0o1000,'BNE'),(0o1400,'BEQ'),(0o2000,'BGE'),(0o2400,'BLT'),(0o3000,'BGT'),(0o3400,'BLE'),
                   (0o100000,'BPL'),(0o100400,'BMI'),(0o101000,'BHI'),(0o101400,'BLOS'),(0o102000,'BVC'),(0o102400,'BVS'),
                   (0o103000,'BCC'),(0o103400,'BCS')]:add(0xff00,base,3,n)
    for i,n in enumerate(('CLR','COM','INC','DEC','NEG','ADC','SBC','TST','ROR','ROL','ASR','ASL')):
        add(0xffc0,0o5000+i*64,1,n);add(0xffc0,0o105000+i*64,1,n+'B')
    for base,n in [(0o100,'JMP'),(0o300,'SWAB'),(0o6700,'SXT'),(0o106400,'MTPS'),(0o106700,'MFPS')]:add(0xffc0,base,1,n)
    add(0xfff8,0o230,10,'SPL');add(0xfff8,0o200,4,'RTS');add(0xfe00,0o4000,5,'JSR');add(0xfe00,0o77000,7,'SOB')
    for base,n in [(0o70000,'MUL'),(0o71000,'DIV'),(0o72000,'ASH'),(0o73000,'ASHC')]:add(0xfe00,base,6,n)
    add(0xfe00,0o74000,5,'XOR');add(0xffc0,0o6400,8,'MARK')
    for base,n in [(0o104000,'EMT'),(0o104400,'TRAP')]:add(0xff00,base,9,n)
    for base,n in [(0o75000,'FADD'),(0o75010,'FSUB'),(0o75020,'FMUL'),(0o75030,'FDIV')]:add(0xfff8,base,4,n)
    # CP77 FP formats: float unary/load/store, integer store/load. Each mode
    # table has FI, DI, FL, DL, then the context-independent DEC generic name.
    for value, name in ((0o170000, 'CFCC'), (0o170001, 'SETF'),
                        (0o170002, 'SETI'), (0o170011, 'SETD'), (0o170012, 'SETL')):
        add(0xffff, value, 0, name)
    for value, name in ((0o170100, 'LDFPS'), (0o170200, 'STFPS'), (0o170300, 'STST')):
        add(0xffc0, value, 1, name)
    for value, name in ((0o170400, 'CLR'), (0o170500, 'TST'),
                        (0o170600, 'ABS'), (0o170700, 'NEG')):
        add(0xffc0, value, 11, (name+'F', name+'D', name+'F', name+'D', name+'f'))
    for value, name, form in ((0o171000,'MUL',12), (0o171400,'MOD',12),
                              (0o172000,'ADD',12), (0o172400,'LD',12),
                              (0o173000,'SUB',12), (0o173400,'CMP',12),
                              (0o174000,'ST',13), (0o174400,'DIV',12)):
        add(0xff00, value, form, (name+'F', name+'D', name+'F', name+'D', name+'f'))
    for value, form, names in (
        (0o175000,14,('STEXP',)*5),
        (0o175400,14,('STCFI','STCDI','STCFL','STCDL','STCfi')),
        (0o176000,13,('STCFD','STCDF','STCFD','STCDF','STCff')),
        (0o176400,15,('LDEXP',)*5),
        (0o177000,15,('LDCIF','LDCID','LDCLF','LDCLD','LDCif')),
        (0o177400,12,('LDCDF','LDCFD','LDCDF','LDCFD','LDCff'))):
        add(0xff00, value, form, names)
    # Table stores the complement of mask, for a single BIC in the decoder.
    text='DTAB:\n'+''.join(f'\t.WORD {mask^65535:o},{value:o},{form:o},DN{i}\n' for i,(mask,value,form,n) in enumerate(rows))
    # An all-ones sentinel distinguishes exact-match entries (complement zero).
    text+='\t.WORD 177777\n'
    for i, (_, _, _, name) in enumerate(rows):
        if isinstance(name, tuple):
            text+='\t.EVEN\n'+f'DN{i}:\t.WORD '+','.join(f'FN{i}{j}' for j in range(5))+'\n'
            text+=''.join(f'FN{i}{j}:\t.ASCIZ /{n}/\n' for j,n in enumerate(name))
        else:
            text+=f'DN{i}:\t.ASCIZ /{name}/\n'
    text+='\t.EVEN\n'
    return text
