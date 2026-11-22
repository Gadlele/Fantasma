# The CPU: what we have seen it do

## 64-bit multiplies among 16-bit loads

On the FM-1 (AC79, pi32v2 at the boot loader's 360 MHz), code that inlines 32x32->64-bit multiplies
(`r9_r8 = r3 * r15 (s)`) among 16-bit loads (`r4 = h[r5+-380] (u)`) now and then leaves a register one bit off,
and the firmware crashes. The crash records show it plainly: a loop index of 0x12 read as 0x00800012, a counter of 0
as 0x00800000, `sp` a word off so that a return pops a neighbouring value. The memory the value came from is
intact.

fm1-radio found it in PacketVideo's AAC decoder, which crashed every few minutes (October 2026). On the device:

| Test (WiFi off) | Result |
| --- | --- |
| SBR's analysis filterbank alone, on fixed input, built at `-O2` | crashed within a minute, every time |
| the same, each run with interrupts off | crashed |
| the same without instruction pairing (`-mllvm -pi32v2-parallel-pack=false`) | crashed |
| the same with a `nop` after each multiply | crashed |
| the same with the multiplies in functions of their own (`noinline`) | 8 minutes clean |
| 1.2 billion 64-bit multiplies, each checked | no error |
| 2 billion 16-bit loads, each checked | no error |
| 16-bit loads beside 64-bit multiplies | crashed within a minute |
| 32-bit loads beside 64-bit multiplies | 2 errors in 4 billion |
| SYSVDD 13 (1.32 V) instead of 15 | crashed as fast |

So keep 64-bit multiplies out of line in hot fixed-point loops: a helper marked `noinline`, as in
`int32_t mul_q31(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 31); }`. The helper uses the product
at once, so the multiply completes before any load that follows. In fm1-radio this cost the decoder about 50% of
its speed (7.8 ms a frame, was 5.1) and made it crash-free.

Not known yet: which exact instruction pairs fail, whether the SDK's 320 MHz is clear of it, and how much other code
is exposed. Floating-point code (minimp3) has no 64-bit multiplies and never showed it. To look for it, list the
functions whose disassembly has `rN_rM = rA * rB` beside `h[...]` loads.
