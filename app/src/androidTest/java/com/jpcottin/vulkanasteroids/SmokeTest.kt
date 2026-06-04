package com.jpcottin.vulkanasteroids

import android.app.NativeActivity
import androidx.lifecycle.Lifecycle
import androidx.test.ext.junit.rules.ActivityScenarioRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import android.graphics.Bitmap
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/**
 * Instrumented smoke test: launches the NativeActivity and verifies that
 * Vulkan initialises without crashing and the activity reaches RESUMED state.
 * A screenshot is captured while the game is in the foreground and saved to
 * external app storage so CI can pull it with `adb pull`.
 */
@RunWith(AndroidJUnit4::class)
class SmokeTest {

    @get:Rule
    val activityRule = ActivityScenarioRule(NativeActivity::class.java)

    @Test
    fun vulkanInitialisesWithoutCrash() {
        // Give the native Vulkan renderer time to complete its first frame.
        Thread.sleep(4_000)

        assertEquals(
            "NativeActivity should be RESUMED after Vulkan init",
            Lifecycle.State.RESUMED,
            activityRule.scenario.state
        )

        // Capture a screenshot while the game is guaranteed to be in the foreground.
        // Write to /sdcard/smoke.png — always accessible via `adb pull` in CI.
        val bitmap = InstrumentationRegistry.getInstrumentation().uiAutomation.takeScreenshot()
        File("/sdcard/smoke.png").outputStream().use { stream ->
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream)
        }
    }
}
