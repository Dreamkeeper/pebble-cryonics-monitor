package org.cryomonitor.companion

import org.junit.Assert.assertEquals
import org.junit.Test

class RebootDrillTest {

    // Fixed reference timeline (wall-clock ms).
    private val armed = 1_000_000_000_000L

    @Test
    fun `not armed`() {
        assertEquals(RebootDrill.Verdict.NotArmed,
            RebootDrill.verdict(0, armed, 5_000, 0, 0, 0))
    }

    @Test
    fun `waiting while no reboot happened since arming`() {
        // Booted long before arming: boot = now - elapsed << armed.
        val now = armed + 60_000
        val elapsed = 3_600_000L
        assertEquals(RebootDrill.Verdict.WaitingForReboot,
            RebootDrill.verdict(armed, now, elapsed, 0, 0, 0))
    }

    @Test
    fun `pass when service came back after the post-arm boot`() {
        val now = armed + 300_000          // 5 min after arming
        val elapsed = 120_000L             // booted 2 min ago (after arming)
        val recoveredAt = armed + 200_000  // service start post-arm
        assertEquals(RebootDrill.Verdict.Pass(42),
            RebootDrill.verdict(armed, now, elapsed,
                recoveredAt - 1_000, recoveredAt, 42))
    }

    @Test
    fun `fail autostart when the receiver never fired after the reboot`() {
        val now = armed + 300_000
        val elapsed = 120_000L
        val staleReceiver = armed - 86_400_000  // yesterday's boot
        assertEquals(RebootDrill.Verdict.FailAutostart,
            RebootDrill.verdict(armed, now, elapsed,
                staleReceiver, staleReceiver, 30))
    }

    @Test
    fun `fail service start when the receiver fired but no boot start followed`() {
        val now = armed + 300_000
        val elapsed = 120_000L
        val receiverAt = armed + 190_000
        val staleRecovery = armed - 86_400_000
        assertEquals(RebootDrill.Verdict.FailServiceStart,
            RebootDrill.verdict(armed, now, elapsed,
                receiverAt, staleRecovery, 30))
    }

    @Test
    fun `boot just around arming needs slack before counting as a reboot`() {
        // Boot 2 s after arming: inside the 5 s clock slack -> still waiting,
        // never a spurious FAIL from clock jitter.
        val now = armed + 60_000
        val elapsed = 58_000L
        assertEquals(RebootDrill.Verdict.WaitingForReboot,
            RebootDrill.verdict(armed, now, elapsed, 0, 0, 0))
    }
}
