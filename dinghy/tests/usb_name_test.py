#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""POLY5's USB name must be one the installers speak to. tools/fm1_install.py (and the web installers after it) only
probe MIDI ports whose name looks like an FM-1 (PORT_RE: "never probe other gear"). A firmware under a name outside
that list runs well, and then cannot be found again to be updated or put back to the official firmware."""
import re, sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
m = re.search(r"#define USB_PRODUCT_STR \{(\d+), 3, (.*)\}", (root / "firmware/src/poly5.c").read_text())
name = "".join(re.findall(r"'(.)', 0", m.group(2)))
port_re = re.search(r'PORT_RE = re.compile\(r"([^"]+)"', (root / "tools/fm1_install.py").read_text()).group(1)
ok = int(m.group(1)) == 2 + 2 * len(name) and re.search(port_re, name, re.I) and len(name) <= 30
print(f'usb name "{name}": the descriptor\'s length is right, the installers probe it' + ("  ok" if ok else "  FAIL"))
sys.exit(0 if ok else 1)
