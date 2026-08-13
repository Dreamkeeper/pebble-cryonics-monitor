package org.cryomonitor.companion

import android.view.View
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat

object Ui {
    /**
     * Android 15+ forces edge-to-edge: without this, content draws under
     * the status bar / camera cutout / gesture bar. Pads the root view by
     * the system-bar + cutout insets; the root's background still paints
     * the full window, so alarm screens stay full-bleed red.
     */
    fun applySystemInsets(root: View) {
        ViewCompat.setOnApplyWindowInsetsListener(root) { v, insets ->
            val bars = insets.getInsets(
                WindowInsetsCompat.Type.systemBars() or
                    WindowInsetsCompat.Type.displayCutout())
            v.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            WindowInsetsCompat.CONSUMED
        }
    }
}
