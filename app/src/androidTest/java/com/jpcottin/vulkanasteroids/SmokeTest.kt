package com.jpcottin.vulkanasteroids

import android.app.NativeActivity
import androidx.lifecycle.Lifecycle
import androidx.test.ext.junit.rules.ActivityScenarioRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Instrumented smoke test: launches the NativeActivity and verifies that
 * Vulkan initialises without crashing and the activity reaches RESUMED state.
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
    }
}
