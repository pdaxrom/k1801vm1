#!/usr/bin/env python3
"""Create a Lattice Programmer XCF for the generated HC1200 JED."""

import argparse
import re
from datetime import datetime
from pathlib import Path
from xml.sax.saxutils import escape


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("jed", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--port", default="FTUSB-0")
    parser.add_argument("--usb-id", default="DUAL RS232 A Location 0000 Serial DUAL RS232 A")
    parser.add_argument("--operation", choices=["FLASH Erase,Program,Verify", "FLASH Verify ID"], default="FLASH Erase,Program,Verify")
    args = parser.parse_args()
    jed = args.jed.resolve()
    matches = re.findall(r"^C([0-9A-Fa-f]+)\*", jed.read_text(), re.MULTILINE)
    if not matches:
        raise SystemExit(f"{jed}: JEDEC checksum record not found")
    checksum = int(matches[-1], 16)
    stamp = datetime.fromtimestamp(jed.stat().st_mtime).strftime("%m/%d/%y %H:%M:%S")
    output = f'''<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE ispXCF SYSTEM "IspXCF.dtd">
<ispXCF version="3.14">
  <Comment>uJ11 HC1200 FRAM/panel/HG RT-11 build</Comment>
  <Chain>
    <Comm>JTAG</Comm>
    <Device>
      <SelectedProg value="TRUE"/><Pos>1</Pos><Vendor>Lattice</Vendor>
      <Family>MachXO2</Family><Name>LCMXO2-1200HC</Name>
      <IDCode>0x012ba043</IDCode><Package>All</Package><PON>LCMXO2-1200HC</PON>
      <Bypass><InstrLen>8</InstrLen><InstrVal>11111111</InstrVal><BScanLen>1</BScanLen><BScanVal>0</BScanVal></Bypass>
      <File>{escape(str(jed))}</File><FileTime>{stamp}</FileTime>
      <JedecChecksum>0x{checksum:04X}</JedecChecksum>
      <Operation>{args.operation}</Operation>
      <Option><SVFVendor>JTAG STANDARD</SVFVendor><IOState>HighZ</IOState>
        <PreloadLength>208</PreloadLength>
        <IOVectorData>0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF</IOVectorData>
        <TCKFrequency>1.000000 MHz</TCKFrequency><Usercode>0x00000000</Usercode>
        <AccessMode>FLASH</AccessMode></Option>
    </Device>
  </Chain>
  <ProjectOptions><Program>SEQUENTIAL</Program><Process>ENTIRED CHAIN</Process>
    <OperationOverride>No Override</OperationOverride><StartTAP>TLR</StartTAP><EndTAP>TLR</EndTAP>
    <VerifyUsercode value="FALSE"/><TCKDelay>30</TCKDelay></ProjectOptions>
  <CableOptions><CableName>USB2</CableName><PortAdd>{escape(args.port)}</PortAdd>
    <USBID>{escape(args.usb_id)}</USBID></CableOptions>
</ispXCF>
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output)
    print(f"Wrote {args.output}: checksum=0x{checksum:04X}, filetime={stamp}")


if __name__ == "__main__":
    main()
