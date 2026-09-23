package dev.zat.benchdeck

import android.os.PowerManager.THERMAL_STATUS_LIGHT
import android.os.PowerManager.THERMAL_STATUS_MODERATE
import android.os.PowerManager.THERMAL_STATUS_NONE
import dev.zat.benchdeck.CoolingPolicy.Decision
import dev.zat.benchdeck.CoolingPolicy.Sample
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CoolingPolicyTest {
    /** 10秒ごとの記録（古い順）。t0 から始まる */
    private fun series(vararg v: Float, t0: Long = 0L) = v.mapIndexed { i, h -> Sample(t0 + i * 10_000L, h) }

    private fun reason(d: Decision) = (d as? Decision.Start)?.reason

    @Test fun idleAboveHalfStartsOnPlateau() {
        // 放置しても 0.62 前後から下がらない端末：以前の「0.5 未満」だと永遠に始まらなかった
        val s = series(0.63f, 0.62f, 0.62f, 0.63f, 0.62f, 0.62f, 0.62f)
        assertEquals("plateau", reason(CoolingPolicy.decide(THERMAL_STATUS_NONE, s, null, 0, 60_000)))
    }

    @Test fun stillCoolingWaits() {
        val s = series(0.90f, 0.86f, 0.82f, 0.78f, 0.74f, 0.70f, 0.66f)
        val d = CoolingPolicy.decide(THERMAL_STATUS_NONE, s, null, 0, 60_000)
        assertTrue(d is Decision.Wait)
        assertEquals(0.24f, (d as Decision.Wait).drop!!, 1e-4f)
    }

    @Test fun waitsUntilWindowIsFilled() {
        val s = series(0.70f, 0.70f, 0.70f)   // まだ20秒分しかない
        assertTrue(CoolingPolicy.decide(THERMAL_STATUS_NONE, s, null, 0, 20_000) is Decision.Wait)
    }

    @Test fun quickStartBelowHalfWithoutThreshold() {
        assertEquals("below_quick_start", reason(CoolingPolicy.decide(THERMAL_STATUS_NONE, series(0.41f), null, 0, 1_000)))
    }

    @Test fun thresholdTakesPrecedenceOverQuickStart() {
        val target = CoolingPolicy.targetFrom(0.45f)!!          // 0.40
        assertEquals(0.40f, target, 1e-6f)
        // 0.43 は 0.5 未満だが、この端末では「軽い制限」の手前 0.05 に入っていないので待つ
        assertTrue(CoolingPolicy.decide(THERMAL_STATUS_NONE, series(0.43f), target, 0, 1_000) is Decision.Wait)
        assertEquals("below_light_threshold", reason(CoolingPolicy.decide(THERMAL_STATUS_NONE, series(0.39f), target, 0, 1_000)))
    }

    @Test fun highThresholdStartsAboveHalf() {
        val target = CoolingPolicy.targetFrom(0.75f)             // 0.70
        assertEquals("below_light_threshold", reason(CoolingPolicy.decide(THERMAL_STATUS_NONE, series(0.62f), target, 0, 1_000)))
    }

    @Test fun moderateNeverStartsBeforeTimeout() {
        val flat = series(0.80f, 0.80f, 0.80f, 0.80f, 0.80f, 0.80f, 0.80f)
        assertTrue(CoolingPolicy.decide(THERMAL_STATUS_MODERATE, flat, null, 0, 60_000) is Decision.Wait)
        assertEquals("timeout", reason(CoolingPolicy.decide(THERMAL_STATUS_MODERATE, flat, null, 0, CoolingPolicy.TIMEOUT_MS)))
    }

    @Test fun lightStatusCanStillPlateau() {
        val flat = series(0.66f, 0.66f, 0.66f, 0.66f, 0.66f, 0.66f, 0.66f)
        assertEquals("plateau", reason(CoolingPolicy.decide(THERMAL_STATUS_LIGHT, flat, null, 0, 60_000)))
    }

    @Test fun historyBeforeWaitCountsForPlateau() {
        // 待ち始める前から放置されていた：待ち始めた瞬間に下がり止まりと分かる
        val s = series(0.61f, 0.61f, 0.61f, 0.61f, 0.61f, 0.61f, 0.61f, t0 = -60_000L)
        assertEquals("plateau", reason(CoolingPolicy.decide(THERMAL_STATUS_NONE, s, null, 0, 0)))
    }

    @Test fun unsupportedAfterGracePeriod() {
        assertTrue(CoolingPolicy.decide(THERMAL_STATUS_NONE, emptyList(), null, 0, 5_000) is Decision.Wait)
        assertEquals("unsupported", reason(CoolingPolicy.decide(THERMAL_STATUS_NONE, emptyList(), null, 0, 15_000)))
    }

    @Test fun invalidThresholdIgnored() {
        assertNull(CoolingPolicy.targetFrom(null))
        assertNull(CoolingPolicy.targetFrom(Float.NaN))
        assertNull(CoolingPolicy.targetFrom(0.03f))
        assertNull(CoolingPolicy.targetFrom(1.5f))
    }
}
