#!/usr/bin/env python3
"""Build dist/index.html: one self-contained file (page + emulator + BIOS + disk image).
Run `make -C os` first so os/portfolio.img is current."""
import base64, html, os, re, subprocess
here = os.path.dirname(os.path.abspath(__file__))
P = lambda *a: os.path.join(here, *a)
b64 = lambda path: base64.b64encode(open(path, "rb").read()).decode()

kernel_bytes = os.path.getsize(P("os", "kernel.bin"))
kernel_sectors = (kernel_bytes + 511) // 512

# real disassembly of the first instructions of boot.bin
dis = subprocess.run(["ndisasm", "-b16", "-e0", P("os", "boot.bin")], capture_output=True, text=True).stdout.splitlines()
rows = []
for line in dis[:20]:
    off, code, asm = line.split(None, 2)
    rows.append('<span class="o">%04X</span>  %-14s <span class="a">%s</span>' % (int(off, 16), code.lower(), html.escape(asm)))
rows.append('<span class="o">  ...</span>')
rows.append('<span class="o">01FE</span>  <span class="sig">55 aa</span>          <span class="a">boot signature</span>')
disasm = "\n".join(rows)

lib = open(P("web", "v86", "libv86.js")).read().replace("</script", "<\\/script")
assets = "window.ASSETS={wasm:\"%s\",bios:\"%s\",vga:\"%s\",img:\"%s\"};" % (
    b64(P("web", "v86", "v86.wasm")), b64(P("web", "v86", "seabios.bin")),
    b64(P("web", "v86", "vgabios.bin")), b64(P("os", "portfolio.img")))

t = open(P("web", "template.html"), encoding="utf-8").read()
for k, v in {"{{KERNEL_BYTES}}": f"{kernel_bytes:,}", "{{KERNEL_SECTORS}}": str(kernel_sectors),
             "{{DISASM}}": disasm, "{{LIBV86}}": lib, "{{ASSETS}}": assets}.items():
    assert k in t, k
    t = t.replace(k, v)
os.makedirs(P("dist"), exist_ok=True)
open(P("dist", "index.html"), "w", encoding="utf-8").write(t)
print("dist/index.html  %.1f MB" % (len(t.encode()) / 1e6))
