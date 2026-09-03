package com.jpcottin.vulkanasteroids

import android.app.NativeActivity
import androidx.lifecycle.Lifecycle
import androidx.test.ext.junit.rules.ActivityScenarioRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import android.graphics.Bitmap
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/**
 * Instrumented smoke test: launches the NativeActivity and verifies that
 * Vulkan initialises without crashing, the activity reaches RESUMED state,
 * and the renderer actually put something on screen (a failed Vulkan init
 * leaves the activity RESUMED over a black surface). A screenshot is captured
 * while the game is in the foreground and saved to external app storage so
 * CI can pull it with `adb pull`.
 */
@RunWith(AndroidJUnit4::class)
class SmokeTest {

    @get:Rule
    val activityRule = ActivityScenarioRule(NativeActivity::class.java)

    @Test
    fun vulkanInitialisesWithoutCrash() {
        // Give the native Vulkan renderer time to complete its first frame.
        Thread.sleep(4_000)

        val state = activityRule.scenario.state
        assertEquals(
            "NativeActivity should be RESUMED after Vulkan init, but was $state",
            Lifecycle.State.RESUMED,
            state
        )

        // Capture a screenshot while the game is guaranteed to be in the foreground.
        // Write to app-specific storage to avoid EPERM on API 30+.
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val screenshotFile = File(context.getExternalFilesDir(null), "smoke.png")

        val bitmap = InstrumentationRegistry.getInstrumentation().uiAutomation.takeScreenshot()
        assertNotNull("takeScreenshot() returned null", bitmap)
        screenshotFile.outputStream().use { stream ->
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream)
        }
        println("Screenshot saved to: ${screenshotFile.absolutePath}")

        // The title screen is a near-black background with bright cyan text and
        // a large ship; sample a grid and require some clearly lit pixels.
        assertTrue(
            "screen is black: Vulkan did not render (see logcat tag Asteroids)",
            litPixelFraction(bitmap) > 0.005f
        )
    }

    /** Fraction of sampled pixels whose max channel exceeds a dark threshold. */
    private fun litPixelFraction(bitmap: Bitmap): Float {
        val step = 8
        var lit = 0
        var total = 0
        for (y in 0 until bitmap.height step step) {
            for (x in 0 until bitmap.width step step) {
                val p = bitmap.getPixel(x, y)
                val max = maxOf((p shr 16) and 0xFF, (p shr 8) and 0xFF, p and 0xFF)
                if (max > 96) lit++
                total++
            }
        }
        return if (total == 0) 0f else lit.toFloat() / total
    }
}
