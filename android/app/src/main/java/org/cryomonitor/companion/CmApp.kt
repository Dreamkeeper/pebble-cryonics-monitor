package org.cryomonitor.companion

import android.app.Application
import com.google.android.material.color.DynamicColors

/**
 * Initializes logging before anything else and captures uncaught exceptions
 * into the CmLog file, so a crash on a phone without adb is still
 * diagnosable via Settings -> View logs (or the log file) after relaunch.
 */
class CmApp : Application() {
    override fun onCreate() {
        super.onCreate()
        CmLog.init(this)
        DynamicColors.applyToActivitiesIfAvailable(this)
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, e ->
            runCatching { CmLog.e("CRASH", "uncaught on ${thread.name}", e) }
            previous?.uncaughtException(thread, e)
        }
    }
}
