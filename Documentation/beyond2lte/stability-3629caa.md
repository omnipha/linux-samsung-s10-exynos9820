# S10+ stability work: kernel 3629caa (issue #44, PR #45)

Status: **3 h synthetic load run passed on kernel 4335ac7b815f (PR #49, merged as `e725b9a77`); practical long run and second unit still open.** Measured on one SM-G975F (`beyond2lte`, host MB007, 8 GB variant, MemTotal 7668376 kB) on 2026-11-24 and 2026-11-25. Kernel 3629caa alone was not stable under load (see below).

## Problem

Under write load (OpenCode work, git clone) the kernel panicked in the ext4 writeback path
(`ext4_end_io_rsv_work -> ext4_finish_bio -> queued_spin_lock_slowpath`, KASAN boot `f2a26704`,
about 5400 s under load) or reported `buffer_head` Padding/Redzone/Poison corruption (#42).
A second S10 (MB013) crashed the same way, so this is not a single-unit hardware fault.
Userspace processes (node, python, git, make, busybox, opencode) died with SIGSEGV/SIGILL/SIGTRAP at the same time.
Evidence: issue #42 (frozen, evidence only).

## Change under test

Kernel `7.2.0-rc2-g3629caacc9e1` (commit `3629caacc9e1`, PR #45, branch `issue-44-stable-power`):

- UFS `vdd-hba`/`vcc`/`vccq`/`vccq2` bound to `reg_dummy` instead of "assuming enabled"; `limit-hs-gear = 2`.
- Panfrost `mali-supply` set.
- CPU idle states and PSCI cpuidle removed; A75 capped at 1508 MHz, Mongoose M4 at 1664 MHz; cpufreq boost off; governor `schedutil`.
- Defconfig without `CPU_IDLE`, `ARM_PSCI_CPUIDLE`, `PM_AUTOSLEEP`, `CPU_FREQ_GOV_POWERSAVE`, `SLUB_DEBUG`; pstore on.

Cause is not isolated: UFS supplies, gear limit, OPP caps and idle removal were changed together.

## Measured so far

| Boot | Kernel | Duration | Kernel BUG/Oops/panic/KASAN | Userspace |
| --- | --- | --- | --- | --- |
| `0a98b819` | `3629caacc9e1` | 63 min (16:31-17:34 UTC), light load only, ended by a deliberate reboot by OpenCode | 0 | Gateway crash loop (NRestarts 19, SEGV/TRAP about every 45 s); `journalctl` segfaulted. Stopped when CPU6/7 were set offline: 0 restarts afterwards. |
| `79176e3f` | `3629caacc9e1` | running since 17:34 UTC | 0 at last check | Gateway 0 restarts at last check, CPU 0-7 online. Too short to judge. |

Observations that are not explained yet:

- Gateway stable with CPU6/7 offline, and (so far) also stable with all 8 CPUs after the next boot. The "mismatched M4 cores kill JIT processes" idea is **not confirmed**.
- Boot log shows `Mismatched cache type (CTR_EL0)` and `Unsupported CPU feature variation` between the boot CPU and CPU6.
- The long ext4 crash needed about 90 min of load; 63 min without a crash is not a pass.

## Proven root-cause mechanism: unreserved firmware debug_snapshot regions (#44)

Physical address analysis of the incident crash dumps and KASAN logs proved direct overlap between corrupted kernel/userspace pages and omitted downstream `debug_snapshot` regions:
- `buffer_head` SLUB corruption (padding, redzone, poison 0x6b overwritten in `prev1.kernel.log.gz` at VA `0xffffff807f43a000`..`0xffffff807f4bdfff` -> PA `0xff43a000`..`0xff4bdfff`, and KASAN panic at VA `0xffffff807f86f3e4` -> PA `0xff86f3e4`) falls directly inside `debug_snapshot.log_kevents` (`0xff400000`–`0xffffffff`, 12 MiB).
- `BUG: Bad page map` in `coredump-worke` (`pte:00000008` with pgd `0xfefed000` and pmd `0xfefee000` in `dmesg-ramoops-2`..`5`) falls directly inside `debug_snapshot.log_llc` (`0xfef10000`–`0xff30ffff`, 4 MiB).

Because these downstream `debug_snapshot` regions (and lower block `0xeb308000`–`0xed210000` plus `ect` at `0x95000000`) were missing from `exynos9820-common.dtsi`, the kernel allocated regular kernel slab objects and user page tables within firmware logging buffers, which Samsung EL3/firmware overwrote asynchronously under system load. Adding these regions to `reserved-memory` with `no-map` prevents Linux from allocating in firmware logging space.

## Result: kernel 4335ac7b815f (PR #49), 3 h load run, 2026-11-25

Kernel `7.2.0-rc2-g4335ac7b815f` = 3629caa plus the `reserved-memory` change for the `debug_snapshot` regions and `ect@95000000` (all `no-map`). Config differs from 3629caa only by toolchain-detected flags (clang 21.1.8).

| Item | Value |
| --- | --- |
| Window | 11:20:12Z - 14:20:18Z (3 h 06 min), boot `13f5b06d`, no reboot |
| Load | on ext4 root (`/dev/sda32`, not tmpfs): 4 gcc workers, 2 node RAM workers (about 96 MiB each, reallocated), 2 ext4 write/readback workers with SHA-256 |
| Load average (n=360, 30 s) | mean 9.80, max 12.43, min 0.79 |
| Fatal signals | 0 (baseline on 3629caa under OpenCode load: 26 in about 2 h) |
| Kernel signatures | 0 Oops/BUG/panic/Call Trace, 0 EXT4-fs error, pstore empty. The only `panic` text in dmesg is the softdog init line (`soft_panic=0`) |
| Checksum/readback mismatches | 0 |
| Gateway | active, `NRestarts=0`, readyz 200, CPUAffinity 0-5 |

Evidence: issue #44, comment 7165480243.

Limits of this result:

- One unit (MB007), one load profile (synthetic: compile, RAM, ext4 I/O), 3 h. This is not a long-term stability statement.
- The earlier failure was never reproduced on demand, so "0 crashes in 3 h" shows a strong change versus the baseline, not proof that the cause is fixed.
- CPU6/7 (Mongoose M4) were online during the run; the gateway stayed pinned to CPU 0-5.
- The address overlap that motivates the patch comes from the old KASAN crash on 22fb7c6 (and its ramoops dumps), not from the userspace crash wave on 3629caa, which left no kernel signature.

## Not yet verified

- Practical workload (OpenCode plus gateway) over many hours on MB007. OpenCode is not installed on the device at the moment.
- MB013 (Galaxy S10, `beyond1lte`): same kernel, same load, not yet run.
- Suspend/resume and thermal behaviour under this load.

## Pass criteria for closing #44

Met on MB007 for 3 h synthetic load (see result above). Still required: practical OpenCode plus gateway run over several hours on MB007, and the same load on MB013. Criteria: 0 kernel BUG/Oops/panic, 0 userspace SEGV wave, gateway 0 unplanned restarts.

## Boot menu (MB007, 2026-11-24)

Since 2026-11-25 the default entry is `pmos-s10-prod-4335ac7b.conf` (kernel 4335ac7b815f). Fallback `pmos-s10-prod-3629caa.conf`, then `805e96e9fe91`, diagnostic KASAN `22fb7c6`, camera test `gc7f24b740a87-dirty` (issue #4).
Older entries moved to `/boot/loader/entries.archiv/` (nothing deleted; backup in `/boot/loader-backup-20261124-tidy`).
Agents must not change the default entry or reboot the device during a load test.
