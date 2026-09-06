Import("env")

from pathlib import Path
import subprocess
import shutil


def check_size(target, source, env):
    elf = Path(str(target[0]))
    size_tool = env.subst("$SIZE") or shutil.which("avr-size") or str(Path(env.subst("$CC")).with_name("avr-size"))
    output = subprocess.check_output([size_tool, "-A", str(elf)], text=True)
    text_size = 0
    data_size = 0
    bss_size = 0
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0] in {".text", ".data", ".rodata"}:
            text_size += int(fields[1])
        if len(fields) >= 3 and fields[0] == ".data":
            data_size += int(fields[1])
        if len(fields) >= 3 and fields[0] == ".bss":
            bss_size = int(fields[1])

    flash_limit = 7000
    ram_limit = 900
    flash_used = text_size
    ram_used = data_size + bss_size
    print("ATmega8 resource gate: flash=%d/%d bytes, RAM=%d/%d bytes" %
          (flash_used, flash_limit, ram_used, ram_limit))
    if flash_used > flash_limit or ram_used > ram_limit:
        raise RuntimeError("ATmega8 safety margin exceeded")


env.AddPostAction("$BUILD_DIR/firmware.elf", check_size)
