# ATMKoala Exp/system UI — local validation record

**Scope.** This record covers the current, unpublished source worktree and the active Limine hybrid ISO path only. It records the completed Exp/system UI batch: navigation and window conveniences, Process Viewer, Mount Manager, Network Profiles, Package Details, and the non-destructive Self-test Dashboard.

> The artifact was built and exercised locally. This is not a release publication, a hardware-certification result, or a claim that untested unsupported facilities work.

## Implemented batch

| Area | Completed implementation | Boundary retained |
|---|---|---|
| Exp navigation | Application hotkeys, `Ctrl+Alt+Left/Right` active-window snap, and front-order `Ctrl+Tab`. | No compositor, virtual desktops, or shortcut-remapping service. |
| Process Viewer | Read-only scheduler snapshot with bounded scroll. | No task kill/suspend/priority operation; resident bytes are not RSS. |
| Mount Manager | CatFS `/data` state, explicit CatFS mount, and confirmed CatFS/Ext2 unmount actions. | No guessed Ext2 drive/partition selection; terminal mount syntax remains required. |
| Network Profiles | Displays saved UserNet rows/state and supports explicit select/save, connect, and disconnect. | No profile editor, wireless scan, background connection, or DHCP-success guarantee. |
| Package Details | Displays configured repository and `installed=yes` local registry metadata. | Read-only: no install/remove/update/dependency solver. HTTP/TLS limits are unchanged. |
| Self-test Dashboard | Explicitly runs six parser/encoding/input checks only. | It does not send traffic, write CMOS/filesystems, mount/unmount, or mutate disks. |

## Verification runs

| Command | Run 1 | Run 2 | Coverage reported by the harness |
|---|---:|---:|---|
| `make all` | Passed | Passed during final ISO rebuild | Freestanding x86-64 kernel link. |
| `make atmloader` | Passed | N/A | Active Limine hybrid ISO construction. |
| `bash tests_qemu_linux_l0.sh` | Passed | Passed | Graphical splash, Linux ABI L0/L1/L3, static-libc, descriptor/process/session, Exp layout, mouse, UDP/NTP/RTC/TZif/package/HTTP parsers, installer/init, hardware-status probes. |
| `bash tests_qemu_ext2_write_type.sh` | Passed | Passed | Persisted guarded direct-block Ext2 write on a generated MBR+Ext2 image, externally checked through `debugfs`. |

## Final local artifacts

| File | Size | SHA-256 |
|---|---:|---|
| `build/kernel.bin` | 752,272 bytes | `0a918e162e3bb155f70e8e4714cd6f1adeb722ec09845ba2bc2372e266440a3e` |
| `atmkoala-OS-v0.9-limine.iso` | 6,141,952 bytes | `768f0b6e0bd933031051e80ab15ef86b08786f2646bbc33bdf878b53502c49ba` |

The source status and compatibility boundaries are maintained in `docs/POSIX_COMPATIBILITY_STATUS.md`. No GitHub synchronization, commit, tag, or publication was performed for this batch.
