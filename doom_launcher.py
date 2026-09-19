#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later OR MIT
"""DooMC0re launcher.

  python doom_launcher.py <PS5_IP>                 launch only
  python doom_launcher.py <PS5_IP> -ftp            launch, then upload wads/
  python doom_launcher.py <PS5_IP> -ftp D:\\my_wads  from somewhere else
  python doom_launcher.py <PS5_IP> -ftp -r         and its subfolders
  python doom_launcher.py <PS5_IP> --list          list what is on the console

The console only opens its FTP server when it finds no WADs, or when R1 is
pressed on the WAD list.
"""
import argparse
import ftplib
import os
import re
import socket
import struct
import sys
import time

PAYLOAD_PORT = 9026
LOG_PORT = 9027
FTP_PORT = 1337
CHUNK_MARK = "--@CHUNK"

SEND_LIMIT = 500 * 1024

WAD_HEADER_SIZE = 12
LUMP_ENTRY_SIZE = 16
LUMP_NAME_OFFSET = 8
MAX_LUMPS = 40000

UPLOAD_BLOCK_SIZE = 32768
FTP_PROBE_TIMEOUT = 2
FTP_PROBE_INTERVAL = 0.5


def human_bytes(byte_count):
    if byte_count < 1024:
        return "%d B" % byte_count
    if byte_count < 1024 * 1024:
        return "%.1f KB" % (byte_count / 1024.0)
    return "%.1f MB" % (byte_count / (1024.0 * 1024.0))


def split_blocks(text, limit):
    blocks = [block for block in text.split(CHUNK_MARK) if block.strip()]
    if not blocks:
        return []

    sends, current = [], ""
    for block in blocks:
        if len(block) > limit:
            sys.exit("[FAIL] a single block is %d bytes, over the %d limit.\n"
                     "       Lower MAX_HEX_PER_PART in make_payload.py."
                     % (len(block), limit))
        if current and len(current) + len(block) > limit:
            sends.append(current)
            current = block
        else:
            current += block
    if current:
        sends.append(current)
    return sends


def is_dotted_quad(host):
    octets = host.split(".")
    return len(octets) == 4 and all(octet.isdigit() and int(octet) < 256
                                    for octet in octets)


def preset_join(text, addr):
    """Rewrite the runner's JOIN_IP/JOIN_PORT so the console starts on it."""
    host, _, port = addr.partition(":")
    if not is_dotted_quad(host):
        sys.exit("[FAIL] --connect wants a dotted quad, got %r" % addr)
    octets = host.split(".")
    if port and not (port.isdigit() and 0 < int(port) < 65536):
        sys.exit("[FAIL] --connect port out of range: %r" % port)

    ip = sum(int(octet) << (i * 8) for i, octet in enumerate(octets))
    text, ip_subs = re.subn(r"^local JOIN_IP   = 0$",
                            "local JOIN_IP   = %d" % ip, text, count=1,
                            flags=re.M)
    text, port_subs = re.subn(r"^local JOIN_PORT = 0$",
                              "local JOIN_PORT = %s" % (port or 0), text,
                              count=1, flags=re.M)
    if not ip_subs or not port_subs:
        sys.exit("[FAIL] no JOIN_IP/JOIN_PORT in the payload; rebuild it")
    return text


def local_ip_toward(host):
    """The address of whichever interface routes to the console."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect((host, PAYLOAD_PORT))
        return sock.getsockname()[0]
    except OSError:
        return None
    finally:
        sock.close()


def preset_log_ip(text, ip):
    """Point the runner's UDP log at this PC instead of the address it was built with."""
    if not is_dotted_quad(ip):
        sys.exit("[FAIL] --log-ip wants a dotted quad, got %r" % ip)
    text, subs = re.subn(r'^(local PC_IP\s+= )"[^"]*"', r'\g<1>"%s"' % ip,
                         text, count=1, flags=re.M)
    if not subs:
        sys.exit("[FAIL] no PC_IP line in the payload; rebuild it")
    return text


def send_payload(host, port, path, limit, delay, timeout, connect=None,
                 log_ip=None):
    with open(path) as f:
        text = f.read()

    if log_ip is None:
        log_ip = local_ip_toward(host)
    if log_ip:
        text = preset_log_ip(text, log_ip)
        print("  console will log to %s:%d" % (log_ip, LOG_PORT))
    else:
        print("  could not find this PC's address; the log target stays as built")

    if connect:
        text = preset_join(text, connect)
        print("  join address preset to %s" % connect)

    sends = split_blocks(text, limit)
    if not sends:
        print("[FAIL] %s has no %s markers" % (os.path.basename(path), CHUNK_MARK))
        return False

    print("  %s, %s -> %d send(s)"
          % (os.path.basename(path), human_bytes(len(text)), len(sends)))

    for i, chunk in enumerate(sends, 1):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        try:
            sock.connect((host, port))
            sock.sendall(chunk.encode())
        except Exception as e:
            print("  [%d/%d] FAILED: %s" % (i, len(sends), e))
            print("  Nothing after this point was sent. The console holds a")
            print("  partial set of globals; re-run to start over.")
            return False
        finally:
            sock.close()

        print("  [%d/%d] %8d bytes" % (i, len(sends), len(chunk)))
        if i != len(sends):
            time.sleep(delay)

    return True


def identify_wad(path):
    try:
        with open(path, "rb") as f:
            header = f.read(WAD_HEADER_SIZE)
            if len(header) < WAD_HEADER_SIZE:
                return ("truncated", False, "shorter than a WAD header")

            magic = header[0:4]
            if magic not in (b"IWAD", b"PWAD"):
                return ("not a WAD", False, "bad magic %r" % magic)

            numlumps, dirofs = struct.unpack_from("<II", header, 4)
            if numlumps == 0 or numlumps > MAX_LUMPS:
                return ("bad directory", False, "%d lumps claimed" % numlumps)

            f.seek(dirofs)
            directory = f.read(numlumps * LUMP_ENTRY_SIZE)
            if len(directory) < numlumps * LUMP_ENTRY_SIZE:
                return ("truncated", False,
                        "lump directory runs past the end of the file")

            names = set()
            for i in range(numlumps):
                entry_start = i * LUMP_ENTRY_SIZE
                raw_name = directory[entry_start + LUMP_NAME_OFFSET:
                                     entry_start + LUMP_ENTRY_SIZE]
                lump_name = raw_name.rstrip(b"\0").upper()
                names.add(lump_name.decode("ascii", "replace"))
    except OSError as e:
        return ("unreadable", False, str(e))

    if magic == b"PWAD":
        return ("PWAD", False, "a patch, not a game -- the loader cannot boot it")

    # the engine I_Errors without these
    for need in ("PLAYPAL", "TEXTURE1", "PNAMES",
                 "F_START", "F_END", "S_START", "S_END"):
        if need not in names:
            return ("incomplete", False, "no %s lump" % need)

    filename = os.path.basename(path).upper()
    is_freedoom = "FREEDOOM" in names

    if "MAP01" in names:
        if "FREEDM" in names:
            return ("FreeDM", True, "")
        if is_freedoom:
            return ("Freedoom Phase 2", True, "")
        if "PLUTONIA" in filename:
            return ("Final Doom: Plutonia", True, "")
        if "TNT" in filename:
            return ("Final Doom: TNT Evilution", True, "")
        if "HACX" in filename:
            return ("HacX", True, "")
        return ("Doom II: Hell on Earth", True, "")

    if "E4M1" in names:
        return ("The Ultimate Doom", True, "")
    if "E2M1" in names:
        return ("Freedoom Phase 1" if is_freedoom else "Doom (Registered)",
                True, "")
    if "E1M1" in names:
        if "CHEX" in filename:
            return ("Chex Quest", True, "")
        return ("Doom Shareware", True, "")

    return ("unrecognised", False, "no E1M1 or MAP01")


def find_wads(folder, recursive):
    wad_paths = []
    if recursive:
        for root, _, files in os.walk(folder):
            wad_paths += [os.path.join(root, name) for name in files
                          if name.lower().endswith(".wad")]
    else:
        wad_paths = [os.path.join(folder, name) for name in os.listdir(folder)
                     if name.lower().endswith(".wad")
                     and os.path.isfile(os.path.join(folder, name))]
    return sorted(wad_paths)


class ConsoleFTP(ftplib.FTP):
    """Ignores the PASV address -- wrong when the console has two interfaces."""

    # keep the port it names, but use the address we already reached it on
    def makepasv(self):
        _, port = super().makepasv()
        return self.host, port


def wait_for_ftp(host, port, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(FTP_PROBE_TIMEOUT)
        try:
            sock.connect((host, port))
            return b"220" in sock.recv(128)
        except Exception:
            time.sleep(FTP_PROBE_INTERVAL)
        finally:
            sock.close()
    return False


def upload_one(ftp, path, quiet):
    size = os.path.getsize(path)
    name = os.path.basename(path)
    sent = [0]
    start_time = time.time()

    def progress(block):
        sent[0] += len(block)
        if quiet:
            return
        percent = sent[0] * 100 // size if size else 100
        sys.stdout.write("\r    %-28s %3d%%  %s"
                         % (name, percent, human_bytes(sent[0])))
        sys.stdout.flush()

    with open(path, "rb") as f:
        ftp.storbinary("STOR " + name, f, blocksize=UPLOAD_BLOCK_SIZE,
                       callback=progress)

    elapsed = max(time.time() - start_time, 0.001)
    if not quiet:
        sys.stdout.write("\r    %-28s done  %s in %.1fs (%s/s)\n"
                         % (name, human_bytes(size), elapsed,
                            human_bytes(size / elapsed)))
    return size


def upload_wads(host, port, folder, recursive, force, quiet, timeout):
    if not os.path.isdir(folder):
        print("[FAIL] not a folder: %s" % folder)
        return 1

    wads = find_wads(folder, recursive)
    if not wads:
        print("[FAIL] no .wad files in %s%s"
              % (folder, " (or below)" if recursive else ""))
        return 1

    try:
        ftp = ConsoleFTP()
        ftp.connect(host, port, timeout=timeout)
        ftp.login()                      # user and password are ignored
        ftp.set_pasv(True)
    except Exception as e:
        print("[FAIL] cannot reach %s:%d: %s" % (host, port, e))
        return 1

    print("  %d WAD%s in %s"
          % (len(wads), "" if len(wads) == 1 else "s", folder))
    queue, skipped = [], 0
    for wad_path in wads:
        desc, usable, note = identify_wad(wad_path)
        size = os.path.getsize(wad_path)
        if usable or force:
            print("    %s %-28s %10s  %s"
                  % (" " if usable else "!", os.path.basename(wad_path),
                     human_bytes(size), desc))
            queue.append(wad_path)
        else:
            print("    - %-28s %10s  %s: %s"
                  % (os.path.basename(wad_path), human_bytes(size), desc, note))
            skipped += 1

    if skipped and not force:
        print("  Skipped %d the console would refuse. --force sends them anyway."
              % skipped)

    if not queue:
        ftp.quit()
        return 1

    print()
    total, failed = 0, 0
    for wad_path in queue:
        try:
            total += upload_one(ftp, wad_path, quiet)
        except Exception as e:
            print("\r    %-28s FAILED: %s" % (os.path.basename(wad_path), e))
            failed += 1

    # not QUIT: clients send that on every disconnect
    closed = False
    if not failed:
        try:
            ftp.sendcmd("SITE EXIT")
            closed = True
        except Exception as e:
            print("    (could not close the server: %s)" % e)

    try:
        ftp.close() if closed else ftp.quit()
    except Exception:
        pass

    print()
    print("  sent %s in %d file(s)%s"
          % (human_bytes(total), len(queue) - failed,
             ", %d failed" % failed if failed else ""))
    print("  Console is rescanning; the WAD list comes back on its own."
          if closed else
          "  Server left running. Re-run to retry, or press Circle.")
    return 1 if failed else 0


def list_console(host, port, timeout):
    try:
        ftp = ConsoleFTP()
        ftp.connect(host, port, timeout=timeout)
        ftp.login()
    except Exception as e:
        print("[FAIL] cannot reach %s:%d: %s" % (host, port, e))
        return 1
    print("On the console:")
    try:
        ftp.retrlines("LIST", lambda line: print("   ", line))
    except Exception as e:
        print("   (LIST failed: %s)" % e)
    ftp.quit()
    return 0


def main():
    parser = argparse.ArgumentParser(description="DooMC0re launcher")
    parser.add_argument("ps5_ip")
    parser.add_argument("-connect", "--connect", default=None, metavar="ADDR",
                        help="preset the netgame join address, e.g. 1.2.3.4 or "
                             "1.2.3.4:2343; saves typing it on the pad")
    parser.add_argument("--log-ip", default=None, metavar="ADDR",
                        help="where the console sends its UDP log; default is "
                             "this PC's address on the console's network")
    parser.add_argument("-ftp", "--ftp", nargs="?", const="wads", default=None,
                        metavar="FOLDER",
                        help="upload WADs after launching; default folder "
                             "is wads/")
    parser.add_argument("-r", "--recursive", action="store_true")
    parser.add_argument("--force", action="store_true",
                        help="upload even files the console will refuse")
    parser.add_argument("--list", action="store_true",
                        help="only show what is already on the console")
    parser.add_argument("--file", default="doom.lua")
    parser.add_argument("--port", type=int, default=PAYLOAD_PORT)
    parser.add_argument("--ftp-port", type=int, default=FTP_PORT)
    parser.add_argument("--limit", type=int, default=SEND_LIMIT,
                        help="bytes per send; must not exceed the console's "
                             "maxsize in remotelualoader.lua")
    parser.add_argument("--delay", type=float, default=1.5,
                        help="seconds between payload sends")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--ftp-wait", type=float, default=25.0,
                        help="seconds to wait for the console's FTP server")
    parser.add_argument("-q", "--quiet", action="store_true")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))

    if args.list:
        return list_console(args.ps5_ip, args.ftp_port, args.timeout)

    path = (args.file if os.path.isabs(args.file)
            else os.path.join(script_dir, args.file))
    if not os.path.isfile(path):
        print("[FAIL] %s not found; run make first"
              % args.file)
        return 1

    print("DOOM PS5 -> %s" % args.ps5_ip)
    print()
    print("[1] Launching")
    if not send_payload(args.ps5_ip, args.port, path,
                        args.limit, args.delay, args.timeout, args.connect,
                        args.log_ip):
        return 1

    if args.ftp is None:
        print()
        print("Watch the UDP log. Expected first lines:")
        print("    === DooMC0re ===")
        print("    All N parts present and correct length")
        print("    Data probe: OK")
        return 0

    folder = (args.ftp if os.path.isabs(args.ftp)
              else os.path.join(script_dir, args.ftp))

    print()
    print("[2] Waiting for the console's FTP server")
    if not wait_for_ftp(args.ps5_ip, args.ftp_port, args.ftp_wait):
        print("  No answer after %gs." % args.ftp_wait)
        print("  It only opens by itself when the console finds no WADs.")
        print("  Press R1 on the WAD list and re-run with -ftp.")
        return 1
    print("  ready")

    print()
    print("[3] Uploading WADs")
    return upload_wads(args.ps5_ip, args.ftp_port, folder, args.recursive,
                       args.force, args.quiet, args.timeout)


if __name__ == "__main__":
    sys.exit(main())
