#!/usr/bin/env python3
"""Offline symbol/PLT inspection of a locally owned Orbis ELF. Never executes it.

Use PYTHONPATH=work/python for the project-local Capstone installation.
Output is kept beside the input under the project work directory.
"""
import argparse
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]


def inspect(path):
    import capstone
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Expected ELF64 little endian')
    header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
    if header[2] != 62:
        raise ValueError('Expected x86-64')
    segments = [struct.unpack_from('<IIQQQQQQ', data, header[5]+i*header[9])
                for i in range(header[10])]
    dynamic = next(s for s in segments if s[0] == 2)
    dynlib = next(s for s in segments if s[0] == 0x61000000)
    tags = dict(struct.unpack_from('<QQ', data, pos)
                for pos in range(dynamic[2], dynamic[2]+dynamic[5], 16))
    def table(tag, size_tag):
        start = dynlib[2]+tags[tag]
        end = start+tags[size_tag]
        if end > len(data):
            raise ValueError('Truncated table')
        return data[start:end]
    strings = table(0x61000035, 0x61000037)
    raw_symbols = table(0x61000039, 0x6100003f)
    names = {}
    for ref in (ROOT/'work/bluetooth-research').glob('idc_*.json'):
        if not ref.name.endswith('sprx.json'):
            continue
        for module in json.loads(ref.read_text(encoding='utf-8-sig')).get('modules', []):
            for lib in module.get('libraries', []):
                for symbol in lib.get('symbols', []):
                    if symbol.get('name'):
                        names[symbol['encoded_id']] = symbol['name']
    names['Q66U8FdrMaw'] = 'scePadGetBluetoothAddress'
    symbols = []
    for pos in range(0, len(raw_symbols), 24):
        name, info, other, section, address, size = struct.unpack_from('<IBBHQQ', raw_symbols, pos)
        raw_name = strings[name:strings.index(b'\0', name)].decode()
        symbols.append(dict(name=names.get(raw_name.split('#')[0], raw_name),
                            nid=raw_name, address=address, size=size, section=section))
    relocations = table(0x61000029, 0x6100002d)
    got = {}
    for pos in range(0, len(relocations), 24):
        offset, info, addend = struct.unpack_from('<QQq', relocations, pos)
        got[offset] = symbols[info >> 32]['name']
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    instructions = []
    for seg in segments:
        if seg[0] == 1 and seg[1] & 1:
            instructions += list(md.disasm(data[seg[2]:seg[2]+seg[5]], seg[3]))
    plt = {}
    for ins in instructions:
        if ins.mnemonic == 'jmp' and len(ins.operands) == 1:
            operand = ins.operands[0]
            if operand.type == capstone.x86.X86_OP_MEM and operand.mem.base == capstone.x86.X86_REG_RIP:
                target = ins.address+ins.size+operand.mem.disp
                if target in got:
                    plt[ins.address] = got[target]
    labels = {s['address']:s['name'] for s in symbols if s['section']}
    lines = []
    for ins in instructions:
        if ins.address in labels:
            lines += ['', labels[ins.address]+':']
        annotation = ''
        if ins.mnemonic in ('call','jmp') and ins.operands[0].type == capstone.x86.X86_OP_IMM:
            annotation = plt.get(ins.operands[0].imm, '')
        lines.append(f'{ins.address:08x}: {ins.mnemonic:10} {ins.op_str} {annotation}'.rstrip())
    output = path.with_suffix('.analysis')
    output.with_suffix('.symbols.json').write_text(json.dumps(symbols, indent=2)+'\n')
    output.with_suffix('.disasm.txt').write_text('\n'.join(lines)+'\n')
    print(f'Inspected {path.name}: {len(symbols)} symbols; no execution')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('elf', type=Path)
    args = parser.parse_args()
    path = args.elf.resolve()
    if not path.is_relative_to(ROOT/'work'):
        parser.error('Keep input and generated analysis under project work/')
    inspect(path)
