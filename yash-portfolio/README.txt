Yash Datir - bootable portfolio (blueprint edition)
===================================================
OPEN:  double-click index.html. It is one self-contained file (page, emulator,
       BIOS and disk image are all inside), so no server or internet is needed
       (fonts fall back to system fonts when offline).
       Click the big power button, then use the screen or the buttons under it.

Folder
  index.html             the site (also fine to upload to GitHub Pages / Netlify as-is)
  Yash_Datir_Resume.pdf  linked from the page
  source/os              bootloader (boot.asm), kernel (kernel.c), linker script, Makefile
  source/web             page template + emulator files
  source/build.py        rebuilds index.html from the template and the disk image

Rebuild after editing the OS (Linux/macOS/WSL, needs nasm, gcc with -m32, ld, make, python3):
  cd source/os && make          # produces portfolio.img
  cd .. && python3 build.py   # writes source/dist/index.html
  Optional: cd os && make run   # test in QEMU

Where the resume text lives in the OS: the RESUME DATA block near the top of kernel.c.
