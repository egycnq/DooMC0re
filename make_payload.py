#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later OR MIT
"""Build doom.lua from doom.elf."""

import os
import re
import struct
import sys

BASE = os.path.dirname(os.path.abspath(__file__))

ELF = os.path.join(BASE, "doom.elf")

MAX_HEX_PER_PART = 160000
LUA_RECV_LIMIT   = 500 * 1024
RELOCS_PER_LINE  = 32
CHUNK_MARK       = "--@CHUNK"

DATA_OFFSET = 0x100000   # linker_split.ld: .data starts here
JIT_GRAIN   = 0x10000    # anything finer than 64K is EINVAL

SHT_RELA     = 4
RELA_ENTSIZE = 24
RELOC_TYPES  = (1, 8)    # R_X86_64_64, R_X86_64_RELATIVE


def read_elf(path):
    if not os.path.isfile(path):
        sys.exit("[FAIL] missing %s -- run `make` first" % os.path.basename(path))
    with open(path, "rb") as elf_file:
        elf = elf_file.read()
    if elf[:4] != b"\x7fELF":
        sys.exit("[FAIL] %s is not an ELF" % os.path.basename(path))

    e_shoff, = struct.unpack_from("<Q", elf, 40)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", elf, 58)

    sections = []
    for i in range(e_shnum):
        name_off, sh_type, _, _, sec_off, size = struct.unpack_from(
            "<IIQQQQ", elf, e_shoff + i * e_shentsize)
        sections.append({"name_off": name_off, "type": sh_type,
                         "off": sec_off, "size": size})

    shstrtab = sections[e_shstrndx]
    strtab = elf[shstrtab["off"]:shstrtab["off"] + shstrtab["size"]]
    for section in sections:
        name_start = section["name_off"]
        name_end = strtab.index(b"\0", name_start)
        section["name"] = strtab[name_start:name_end].decode()

    def section_bytes(wanted_name):
        for section in sections:
            if section["name"] == wanted_name:
                return elf[section["off"]:section["off"] + section["size"]]
        sys.exit("[FAIL] %s has no %s section"
                 % (os.path.basename(path), wanted_name))

    # only .data needs fixing up; code is rip-relative
    reloc_offsets = set()
    for section in sections:
        if section["type"] != SHT_RELA:
            continue
        rela = elf[section["off"]:section["off"] + section["size"]]
        for i in range(0, len(rela) - (RELA_ENTSIZE - 1), RELA_ENTSIZE):
            r_off, r_info, _ = struct.unpack_from("<QQq", rela, i)
            if (r_info & 0xFFFFFFFF) in RELOC_TYPES and r_off >= DATA_OFFSET:
                reloc_offsets.add(r_off)

    return section_bytes(".text"), section_bytes(".data"), sorted(reloc_offsets)


def split_even(total, budget, align=1):
    part_count = max(1, (total + budget - 1) // budget)
    units = (total + align - 1) // align
    units_per_part, extra_units = divmod(units, part_count)
    chunks, pos = [], 0
    for i in range(part_count):
        length = (units_per_part + (1 if i < extra_units else 0)) * align
        if pos + length > total:
            length = total - pos
        chunks.append((pos, length))
        pos += length
    return [chunk for chunk in chunks if chunk[1] > 0]


def main():
    code, data, reloc_offsets = read_elf(ELF)

    # Firmware 13.02 gives the process well under 1MB of JIT, so ask for what
    # the code needs plus one spare grain rather than a round 0x100000.
    jit_size = ((len(code) + JIT_GRAIN - 1) & ~(JIT_GRAIN - 1)) + JIT_GRAIN

    data_init = len(data)
    while data_init > 0 and data[data_init - 1] == 0:
        data_init -= 1
    data_init = (data_init + 3) & ~3
    if data_init < 8:
        sys.exit("[FAIL] .data looks empty (%d initialised bytes)" % data_init)

    deltas, prev_offset = [], 0
    for offset in reloc_offsets:
        deltas.append(offset - prev_offset)
        prev_offset = offset

    past_prefix = sum(1 for offset in reloc_offsets
                      if offset - DATA_OFFSET >= data_init)

    probe_start = struct.unpack_from("<I", data, 0)[0]
    probe_64 = struct.unpack_from("<I", data, 64)[0]
    probe_end = struct.unpack_from("<I", data, data_init - 4)[0]

    parts = []          # (global_name, hex_len) in send order
    blocks = []         # source blocks, each safe to send on its own

    code_chunks = split_even(len(code), MAX_HEX_PER_PART // 2)
    data_chunks = split_even(data_init, MAX_HEX_PER_PART // 2)

    for i, (offset, length) in enumerate(code_chunks):
        global_name = "DOOM_C%d" % (i + 1)
        hex_text = code[offset:offset + length].hex().upper()
        parts.append((global_name, len(hex_text)))
        blocks.append('-- code %d/%d\n%s = "%s"\n'
                      % (i + 1, len(code_chunks), global_name, hex_text))

    for i, (offset, length) in enumerate(data_chunks):
        global_name = "DOOM_D%d" % (i + 1)
        hex_text = data[offset:offset + length].hex().upper()
        parts.append((global_name, len(hex_text)))
        blocks.append('-- data %d/%d\n%s = "%s"\n'
                      % (i + 1, len(data_chunks), global_name, hex_text))

    reloc_lines = []
    for i in range(0, len(deltas), RELOCS_PER_LINE):
        row = deltas[i:i + RELOCS_PER_LINE]
        reloc_lines.append(",".join(str(delta) for delta in row) + ",")

    want_lines = "\n".join('  {"%s", %d},' % (global_name, hex_len)
                           for global_name, hex_len in parts)
    code_globals = "\n".join(
        "write_shellcode(rw + code_off, %s); code_off = code_off + (#%s // 2)"
        % (global_name, global_name)
        for global_name, _ in parts if global_name.startswith("DOOM_C"))
    data_globals = "\n".join(
        "put_data(%s)" % global_name
        for global_name, _ in parts if global_name.startswith("DOOM_D"))
    release = "\n".join("%s = nil" % global_name for global_name, _ in parts)

    runner = RUNNER_TEMPLATE.format(
        reloc="\n".join(reloc_lines),
        nparts=len(parts),
        want=want_lines,
        jit_size=hex(jit_size),
        data_offset=hex(DATA_OFFSET),
        code_size=len(code),
        data_size=len(data),
        data_init=data_init,
        code_writes=code_globals,
        data_writes=data_globals,
        release=release,
        p1=hex(probe_start), p2=hex(probe_64), p3=hex(probe_end),
        nreloc=len(deltas),
    )
    blocks.append(runner)

    out_path = os.path.join(BASE, "doom.lua")
    with open(out_path, "w") as lua_file:
        lua_file.write("-- DooMC0re\n")
        lua_file.write("-- Send with: python doom_launcher.py <PS5_IP>\n")
        for block in blocks:
            lua_file.write(CHUNK_MARK + "\n")
            lua_file.write(block)

    out_size = os.path.getsize(out_path)

    verify(code, data, data_init, reloc_offsets, parts,
           probe_start, probe_64, probe_end)

    print("code   %7d bytes -> %d block(s)" % (len(code), len(code_chunks)))
    print("data   %7d bytes mapped, %d initialised -> %d block(s)"
          % (len(data), data_init, len(data_chunks)))
    print("relocs %7d entries%s"
          % (len(deltas), "" if not past_prefix
             else "  (%d past prefix, skipped at runtime)" % past_prefix))
    print("probe  %s %s %s" % (hex(probe_start), hex(probe_64), hex(probe_end)))
    print()
    print("wrote doom.lua, %d bytes in %d block(s)" % (out_size, len(blocks)))
    if out_size <= LUA_RECV_LIMIT:
        print("  fits LuaC0re's %d-byte receive buffer -- goes in one send"
              % LUA_RECV_LIMIT)
    else:
        print("  larger than LuaC0re's %d-byte receive buffer, so doom_launcher.py"
              % LUA_RECV_LIMIT)
        print("  splits it on the chunk markers. To make it a single send,")
        print("  raise maxsize in /savedata0/lua/remotelualoader.lua and pass")
        print("  --limit to doom_launcher.py to match.")
    if jit_size > DATA_OFFSET:
        print("[WARN] the JIT region (%s) now reaches the data mmap at rx+%s."
              % (hex(jit_size), hex(DATA_OFFSET)))
        print("       Move .data further out in linker_split.ld and DATA_OFFSET.")
    print("next:  python udp_log.py 9027 doom_test.log")
    print("       python doom_launcher.py <PS5_IP>")
    return 0


def verify(code, data, data_init, reloc_offsets, parts,
           probe_start, probe_64, probe_end):
    """Reassemble the parts the way the runner will, and compare."""
    with open(os.path.join(BASE, "doom.lua")) as runner_file:
        runner_text = runner_file.read()

    globals_found = {}
    for match in re.finditer(r'^(DOOM_[CD]\d+) = "([0-9A-F]*)"',
                             runner_text, re.M):
        globals_found[match.group(1)] = match.group(2)
    if not globals_found:
        sys.exit("[FAIL] doom.lua contains no payload globals")

    if runner_text.count(CHUNK_MARK) != len(globals_found) + 1:
        sys.exit("[FAIL] %d chunk markers for %d globals plus the runner"
                 % (runner_text.count(CHUNK_MARK), len(globals_found)))

    declared = dict(parts)
    if set(globals_found) != set(declared):
        sys.exit("[FAIL] emitted globals %s != runner's WANT list %s"
                 % (sorted(globals_found), sorted(declared)))
    for global_name, hex_text in globals_found.items():
        if len(hex_text) != declared[global_name]:
            sys.exit("[FAIL] %s is %d hex chars, runner expects %d"
                     % (global_name, len(hex_text), declared[global_name]))

    code_block_count = sum(1 for name in globals_found
                           if name.startswith("DOOM_C"))
    rebuilt_code = b"".join(bytes.fromhex(globals_found["DOOM_C%d" % (i + 1)])
                            for i in range(code_block_count))
    if rebuilt_code != code:
        sys.exit("[FAIL] code parts do not reassemble to .text")

    data_block_count = sum(1 for name in globals_found
                           if name.startswith("DOOM_D"))
    rebuilt_data = bytearray()
    for i in range(data_block_count):
        hex_text = globals_found["DOOM_D%d" % (i + 1)]
        if len(hex_text) % 2:
            sys.exit("[FAIL] DOOM_D%d has an odd hex length" % (i + 1))
        rebuilt_data += bytes.fromhex(hex_text)
    if bytes(rebuilt_data) != data[:data_init]:
        sys.exit("[FAIL] data parts do not reassemble to the .data prefix")
    if len(rebuilt_data) != data_init:
        sys.exit("[FAIL] reassembled data is %d bytes, DATA_INIT is %d"
                 % (len(rebuilt_data), data_init))

    if (struct.unpack_from("<I", rebuilt_data, 0)[0] != probe_start or
            struct.unpack_from("<I", rebuilt_data, 64)[0] != probe_64 or
            struct.unpack_from("<I", rebuilt_data,
                               data_init - 4)[0] != probe_end):
        sys.exit("[FAIL] probe words do not match the reassembled data")

    match = re.search(r"local RELOC = \{(.*?)\n\}", runner_text, re.S)
    if not match:
        sys.exit("[FAIL] cannot find RELOC table in the generated runner")
    fields = match.group(1).replace("\n", "").split(",")
    deltas = [int(field) for field in fields if field.strip()]
    offset, replayed = 0, []
    for delta in deltas:
        offset += delta
        replayed.append(offset)
    if replayed != reloc_offsets:
        sys.exit("[FAIL] RELOC deltas do not replay to the original offsets")

    for token in ("local PC_IP    = \"",
                  "CODE_SIZE   = %d" % len(code),
                  "DATA_INIT   = %d" % data_init,
                  "local p1 = read32(data_addr)"):
        if token not in runner_text:
            sys.exit("[FAIL] generated runner is missing '%s'" % token)

    print("verify OK: %d code block(s), %d data block(s), %d relocs replay clean"
          % (code_block_count, data_block_count, len(deltas)))


RUNNER_TEMPLATE = '''local RELOC = {{
{reloc}
}}

init_dlsym()
sceMsgDialogTerminate()

local PC_IP    = "192.168.1.120"   -- doom_launcher.py rewrites this to the sending PC
local LOG_PORT = 9027
local NET_PORT = 2342      -- Doom's default netgame port

local SOCKADDR_LEN = 16

local function htons(port) return ((port << 8) | (port >> 8)) & 0xFFFF end
local function hex(value) return string.format("0x%x", value) end
local function inet_addr(ip_text)
    local a,b,c,d = ip_text:match("(%d+)%.(%d+)%.(%d+)%.(%d+)")
    a, b, c, d = tonumber(a), tonumber(b), tonumber(c), tonumber(d)
    return (d << 24) | (c << 16) | (b << 8) | a
end

local function make_sockaddr_in(port, ip)
    local sockaddr = malloc(SOCKADDR_LEN)
    for i = 0, SOCKADDR_LEN - 1 do write8(sockaddr + i, 0) end
    write8(sockaddr + 0, SOCKADDR_LEN)
    write8(sockaddr + 1, 2)
    write16(sockaddr + 2, htons(port))
    if ip then write32(sockaddr + 4, inet_addr(ip)) end
    return sockaddr
end

local log_sock = create_socket(AF_INET, SOCK_DGRAM, 0)
local log_sockaddr = make_sockaddr_in(LOG_PORT, PC_IP)

local function ulog(message)
    if log_sock >= 0 then
        syscall.sendto(log_sock, message.."\\n", #message+1, 0,
                       log_sockaddr, SOCKADDR_LEN)
    end
end

ulog("=== DooMC0re ===")

local SOL_SOCKET   = 0xFFFF
local SO_REUSEADDR = 0x0004
local SO_BROADCAST = 0x0020
local SO_REUSEPORT = 0x0200

local sockopt_true = malloc(4)
write32(sockopt_true, 1)
local function sockopt_on(sock, opt)
    syscall.setsockopt(sock, SOL_SOCKET, opt, sockopt_true, 4)
end

-- socket() is blocked inside the payload from firmware 8.00, so every socket
-- the C side uses is opened here and passed down in ext_args.
local function make_listener(port)
    local sock = create_socket(AF_INET, 1, 0)       -- SOCK_STREAM
    if sock < 0 then
        ulog("socket(" .. port .. ") failed")
        return -1
    end
    sockopt_on(sock, SO_REUSEADDR)
    sockopt_on(sock, SO_REUSEPORT)
    local bind_ret = syscall.bind(sock, make_sockaddr_in(port), SOCKADDR_LEN)
    syscall.listen(sock, 128)
    ulog("ftp listener port " .. port .. " fd=" .. tostring(sock)
         .. " bind=" .. tostring(bind_ret))
    return sock
end

local ftp_srv  = make_listener(1337)
local ftp_data = make_listener(1338)

local net_fd = create_socket(AF_INET, SOCK_DGRAM, 0)
if net_fd >= 0 then
    sockopt_on(net_fd, SO_REUSEADDR)
    -- or sendto 255.255.255.255 fails EACCES
    sockopt_on(net_fd, SO_BROADCAST)
    local bind_ret = syscall.bind(net_fd, make_sockaddr_in(NET_PORT),
                                  SOCKADDR_LEN)
    ulog("net udp port " .. NET_PORT .. " fd=" .. tostring(net_fd)
         .. " bind=" .. tostring(bind_ret))
else
    ulog("net udp socket failed")
end

local WANT = {{
{want}
}}

local missing, truncated = 0, 0
for i = 1, #WANT do
    local part_name, want_len = WANT[i][1], WANT[i][2]
    local part = _G[part_name]
    if part == nil then
        ulog("[FAIL] missing part global " .. part_name)
        missing = missing + 1
    elseif #part ~= want_len then
        ulog("[FAIL] " .. part_name .. " truncated: got " .. #part
             .. " expected " .. want_len)
        truncated = truncated + 1
    end
end
if missing > 0 or truncated > 0 then
    ulog("[ABORT] " .. missing .. " missing, " .. truncated .. " truncated."
         .. " Re-send doom.lua.")
    return
end
ulog("All {nparts} parts present and correct length")

local JIT_SIZE    = {jit_size}
local DATA_OFFSET = {data_offset}
local CODE_SIZE   = {code_size}
local DATA_SIZE   = {data_size}
local DATA_INIT   = {data_init}

if not sceKernelLoadStartModule then
    sceKernelLoadStartModule =
        func_wrap(dlsym(LIBKERNEL_HANDLE, "sceKernelLoadStartModule"))
end
local libUser = sceKernelLoadStartModule("libSceUserService.sprx", 0, 0, 0, 0, 0)
local getUserId = dlsym(libUser, "sceUserServiceGetInitialUser")
local uid_buf = malloc(4)
write32(uid_buf, 0)
if getUserId then func_wrap(getUserId)(uid_buf) end
local userId = read32(uid_buf)
ulog("userId=" .. tostring(userId))

local base_fd     = jit_malloc(8)
local rw_fd       = jit_malloc(8)
local rx_fd       = jit_malloc(8)
local rw_addr_buf = jit_malloc(8)
local rx_addr_buf = malloc(8)
local name_buf    = jit_malloc(8)
jit_write_buffer(name_buf, "doom")

jit_sceKernelJitCreateSharedMemory(name_buf, JIT_SIZE, 7, base_fd)
jit_sceKernelJitCreateAliasOfSharedMemory(
    jit_read32(base_fd), PROT_READ|PROT_WRITE, rw_fd)
jit_sceKernelJitCreateAliasOfSharedMemory(
    jit_read32(base_fd), PROT_READ|PROT_EXECUTE, rx_fd)
jit_sceKernelJitMapSharedMemory(
    jit_read32(rw_fd), PROT_READ|PROT_WRITE, rw_addr_buf)
local rw = jit_read64(rw_addr_buf)

local main_fd = jit_send_recv_fd(jit_read32(rx_fd), NEW_JIT_SOCK, NEW_MAIN_SOCK)
sceKernelJitMapSharedMemory(main_fd, PROT_READ|PROT_EXECUTE, rx_addr_buf)
local rx = read64(rx_addr_buf)

ulog("rx=" .. hex(rx) .. "  rw=" .. hex(rw))
if rx == 0 or rw == 0 then
    ulog("[FAIL] JIT mapping returned 0")
    return
end

local code_off = 0
{code_writes}

ulog("Code: " .. code_off .. " / " .. CODE_SIZE .. " bytes")
if code_off ~= CODE_SIZE then
    ulog("[FAIL] code size mismatch")
    return
end

local data_map_size = (DATA_SIZE + 0xFFF) & ~0xFFF
local data_target   = rx + DATA_OFFSET
local data_addr     = syscall.mmap(data_target, data_map_size, 3, 0x1012, -1, 0)
if data_addr ~= data_target then
    ulog("[FAIL] MAP_FIXED mmap: wanted " .. hex(data_target)
         .. " got " .. hex(data_addr))
    return
end

local data_off = 0
local function put_data(hex_text)
    local blob = hex_to_binary(hex_text)
    write_buffer(data_addr + data_off, blob)
    data_off = data_off + #blob
end
{data_writes}

ulog("Data: " .. data_off .. " / " .. DATA_INIT .. " initialised ("
     .. DATA_SIZE .. " mapped)")
if data_off ~= DATA_INIT then
    ulog("[FAIL] data size mismatch")
    return
end

local p1 = read32(data_addr)
local p2 = read32(data_addr + 64)
local p3 = read32(data_addr + DATA_INIT - 4)
local probe_ok = (p1 == {p1}) and (p2 == {p2}) and (p3 == {p3})
ulog("Data probe: " .. (probe_ok and "OK" or
     string.format("MISMATCH %x %x %x", p1, p2, p3)))
if not probe_ok then
    ulog("[FAIL] data readback wrong - aborting")
    return
end

{release}

local applied, skipped = 0, 0
local reloc_off = 0
for i = 1, #RELOC do
    reloc_off = reloc_off + RELOC[i]
    local ptr = data_addr + (reloc_off - DATA_OFFSET)
    local value = read64(ptr)
    if value ~= 0 then
        write64(ptr, value + rx)
        applied = applied + 1
    else
        skipped = skipped + 1
    end
end
ulog("Relocs: " .. applied .. " applied, " .. skipped .. " skipped (of {nreloc})")

-- ext_args, as struct ext_args in platform.h reads it:
--   +0x10 u32 frame_count   written back by C
--   +0x18 s32 log_fd
--   +0x20 u8  log_addr[16]  sockaddr_in for the UDP log target
--   +0x48 u64 handoff[3]    userId
--   +0x50 u64 handoff[4]    FTP control fd (1337)
--   +0x58 u64 handoff[5]    FTP data fd (1338)
--   +0x60 u64 handoff[6]    netgame UDP fd (2342)
--   +0x68 u32 handoff[7]    this console's IP, wire order; 0 if unknown
local ext = malloc(0x80)
for i = 0, 0x7F do write8(ext+i, 0) end
write32(ext+0x18, log_sock)
write32(ext+0x1C, -1)
for i = 0, SOCKADDR_LEN - 1 do write8(ext+0x20+i, read8(log_sockaddr+i)) end
write64(ext+0x48, userId)
write64(ext+0x50, ftp_srv  >= 0 and ftp_srv  or -1)
write64(ext+0x58, ftp_data >= 0 and ftp_data or -1)
write64(ext+0x60, net_fd   >= 0 and net_fd   or -1)
local my_ip = get_current_ip()
write32(ext+0x68, my_ip and inet_addr(my_ip) or 0)

-- doom_launcher.py --connect rewrites these two before sending
local JOIN_IP   = 0
local JOIN_PORT = 0
write32(ext+0x70, JOIN_IP)
write32(ext+0x74, JOIN_PORT)
-- the port the socket above is actually bound to, so C never has to guess
write32(ext+0x78, NET_PORT)

ulog("IP: " .. tostring(my_ip))
if JOIN_IP ~= 0 then ulog("join address preset by launcher") end
send_notification("DOOM PS5 starting\\nlog -> " .. PC_IP .. ":" .. LOG_PORT)

ulog("Jumping to _start (rx=" .. hex(rx) .. ")")
func_wrap(rx)(EBOOT_BASE, SCE_KERNEL_DLSYM, ext)

local frame_count = read32(ext+0x10)
ulog("=== DOOM returned ===  frame_count=" .. tostring(frame_count))
send_notification("DOOM exited (" .. tostring(frame_count) .. " frames)")
'''


if __name__ == "__main__":
    sys.exit(main())
