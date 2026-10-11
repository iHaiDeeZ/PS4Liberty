"""Upload an installed GTA IV tree (made by the PC build's installer) to the PS4 over FTP.

The PS4 build skips the installer: it starts directly when /data/LibertyRecomp/game holds a
valid default.xex + default.xexp pair (IsInstallReady). This copies <local game dir> there with
several parallel connections, skips files already present with the same size (resumable), and
sends default.xex / default.xexp last so a partial upload never looks like a ready install.

Usage: py -3 upload-ps4-game.py <local game dir> <PS4 IP> [port] [remote dir] [connections]
"""
import ftplib
import pathlib
import queue
import sys
import threading
import time

LAST = {"default.xex", "default.xexp"}


def connect(host, port):
    ftp = ftplib.FTP()
    ftp.connect(host, port, timeout=60)
    ftp.login()
    ftp.voidcmd("TYPE I")
    return ftp


def remote_size(ftp, path):
    try:
        return ftp.size(path)
    except ftplib.all_errors:
        return None


def ensure_dirs(ftp, remote_root, directories):
    for directory in [remote_root] + sorted(directories, key=lambda d: d.count("/")):
        path = remote_root if directory == remote_root else f"{remote_root}/{directory}"
        try:
            ftp.mkd(path)
        except ftplib.error_perm:
            pass  # exists


def upload(ftp, local, remote, counters, lock):
    size = local.stat().st_size
    if remote_size(ftp, remote) == size:
        with lock:
            counters["skipped"] += size
        return
    with open(local, "rb") as f:
        ftp.storbinary(f"STOR {remote}", f, blocksize=1 << 20,
                       callback=lambda chunk: _count(counters, lock, len(chunk)))
    if remote_size(ftp, remote) != size:
        raise RuntimeError(f"size mismatch after upload: {remote}")


def _count(counters, lock, n):
    with lock:
        counters["sent"] += n


def main():
    local_root = pathlib.Path(sys.argv[1])
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    host = sys.argv[2]
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 2121
    remote_root = sys.argv[4] if len(sys.argv) > 4 else "/data/LibertyRecomp/game"
    connections = int(sys.argv[5]) if len(sys.argv) > 5 else 4

    files = [p for p in local_root.rglob("*") if p.is_file()]
    relative = {p: p.relative_to(local_root).as_posix() for p in files}
    # Every ancestor too: a folder holding only subfolders (xbox360/audio) must exist before its
    # children, and the PS4's FTP server stalls on STOR into a missing folder instead of failing.
    directories = set()
    for r in relative.values():
        directories.update(str(p) for p in pathlib.PurePosixPath(r).parents)
    directories -= {"."}
    total = sum(p.stat().st_size for p in files)
    first = sorted((p for p in files if relative[p] not in LAST), key=lambda p: -p.stat().st_size)
    last = [p for p in files if relative[p] in LAST]
    print(f"{len(files)} files, {total / 2**30:.2f} GiB -> {host}:{port}{remote_root}", flush=True)

    ftp = connect(host, port)
    ensure_dirs(ftp, remote_root, directories)
    ftp.quit()

    counters = {"sent": 0, "skipped": 0}
    lock = threading.Lock()
    errors = []
    work = queue.Queue()
    for p in first:
        work.put(p)

    def worker():
        ftp = connect(host, port)
        while True:
            try:
                p = work.get_nowait()
            except queue.Empty:
                break
            for attempt in range(3):
                try:
                    upload(ftp, p, f"{remote_root}/{relative[p]}", counters, lock)
                    break
                except Exception as error:  # reconnect and retry
                    if attempt == 2:
                        errors.append(f"{relative[p]}: {error}")
                    try:
                        ftp.close()
                    except Exception:
                        pass
                    time.sleep(2)
                    ftp = connect(host, port)
        try:
            ftp.quit()
        except Exception:
            pass

    start = time.time()
    threads = [threading.Thread(target=worker) for _ in range(connections)]
    for t in threads:
        t.start()
    while any(t.is_alive() for t in threads):
        time.sleep(30)
        with lock:
            done = counters["sent"] + counters["skipped"]
            rate = counters["sent"] / max(1, time.time() - start) / 2**20
        print(f"{done / 2**30:.2f}/{total / 2**30:.2f} GiB, {rate:.1f} MiB/s", flush=True)
    for t in threads:
        t.join()
    if errors:
        print("FAILED:\n" + "\n".join(errors), flush=True)
        sys.exit(1)

    ftp = connect(host, port)  # the readiness pair goes last
    for p in last:
        upload(ftp, p, f"{remote_root}/{relative[p]}", counters, lock)
    ftp.quit()
    print(f"done: {len(files)} files in {(time.time() - start) / 60:.1f} min", flush=True)


main()
