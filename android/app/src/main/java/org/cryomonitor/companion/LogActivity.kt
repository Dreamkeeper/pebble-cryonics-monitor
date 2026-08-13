package org.cryomonitor.companion

import android.app.Activity
import android.content.Intent
import android.graphics.Typeface
import android.os.Bundle
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/** In-app log viewer with share/clear (no file manager needed). */
class LogActivity : Activity() {

    private lateinit var text: TextView
    private lateinit var scroll: ScrollView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        row.addView(Button(this).apply {
            text = "Refresh"; setOnClickListener { refresh() }
        })
        row.addView(Button(this).apply {
            text = "Share"
            setOnClickListener {
                startActivity(Intent.createChooser(
                    Intent(Intent.ACTION_SEND).apply {
                        type = "text/plain"
                        putExtra(Intent.EXTRA_SUBJECT, "Cryonics Monitor logs")
                        putExtra(Intent.EXTRA_TEXT, CmLog.dump())
                    }, "Share logs"))
            }
        })
        row.addView(Button(this).apply {
            text = "Clear"; setOnClickListener { CmLog.clear(); refresh() }
        })
        col.addView(row)

        text = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 11f
            setPadding(16, 16, 16, 16)
        }
        scroll = ScrollView(this).apply { addView(text) }
        col.addView(scroll)
        Ui.applySystemInsets(col)
        setContentView(col)
        refresh()
    }

    private fun refresh() {
        text.text = CmLog.dump().ifEmpty { "(no log lines yet)" }
        scroll.post { scroll.fullScroll(ScrollView.FOCUS_DOWN) }
    }
}
