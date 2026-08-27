package org.cryomonitor.companion

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Bundle
import android.os.VibrationEffect
import android.os.Vibrator
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import org.cryomonitor.companion.Ui.caption
import org.cryomonitor.companion.Ui.title
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Everything debug- and feasibility-related in one place (owner request,
 * M0 test round): debug toggle, latency drill (S1), the guided S4
 * sensor lab with recording + share, live S5 DataLogging stats, and the
 * S6 battery-drain estimate.
 */
class DebugActivity : AppCompatActivity() {

    private lateinit var settings: SettingsStore

    // ---- S4 sensor lab state ----
    private data class Stage(val key: String, val instruction: String,
                             val seconds: Int)

    private val stages = listOf(
        Stage("worn_moving", "Wear the watch snugly.\nMove your arm normally.", 120),
        Stage("worn_still", "Keep wearing it.\nRest your arm — perfectly still.", 180),
        Stage("strap_loose", "Loosen the strap by two holes.\nRest your arm again.", 120),
        Stage("table_flat", "Take the watch OFF.\nLay it screen-UP on the table.", 180),
        Stage("table_facedown", "Flip the watch screen-DOWN\n(sensor facing up).", 120),
        Stage("fabric", "Press the sensor side\nagainst clothing or fabric.", 120))

    private var labRunning = false
    private var stageIdx = -1
    private var stageEndsAt = 0L
    private var labStartedAt = 0L
    private val csv = StringBuilder()
    private val stageSamples = HashMap<String, MutableList<Int>>()
    private val stageAges = HashMap<String, MutableList<Int>>()
    private var minHeap = Int.MAX_VALUE
    private var lastResultFile: File? = null

    private lateinit var labInstruction: TextView
    private lateinit var labLive: TextView
    private lateinit var labBtn: Button
    private lateinit var shareBtn: Button
    private lateinit var s5Line: TextView
    private lateinit var s6Line: TextView

    private val sampleReceiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context?, intent: Intent?) {
            intent ?: return
            val bpm = intent.getIntExtra("bpm", 0)
            val age = intent.getIntExtra("event_age_s", -1)
            val heap = intent.getIntExtra("heap", 0)
            onLabSample(bpm, age, heap)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        settings = SettingsStore(this)

        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(Ui.dp(context, 16), Ui.dp(context, 16),
                       Ui.dp(context, 16), Ui.dp(context, 16))
        }
        fun header(t: String) = col.addView(TextView(this).apply {
            text = t; title()
            setPadding(0, Ui.dp(context, 24), 0, Ui.dp(context, 8))
        })

        header("Diagnostics")
        @Suppress("UseSwitchCompatOrMaterialCode")
        col.addView(Switch(this).apply {
            text = "Debug mode (extensive logs, phone + watch)"
            isChecked = settings.debugLogging
            setOnCheckedChangeListener { _, on ->
                settings.debugLogging = on
                startService(Intent(this@DebugActivity, MonitorService::class.java)
                    .setAction(MonitorService.ACTION_SET_DEBUG)
                    .putExtra("enabled", on))
            }
        })
        col.addView(Button(this).apply {
            text = "View logs"
            setOnClickListener {
                startActivity(Intent(this@DebugActivity, LogActivity::class.java))
            }
        })

        header("S1 — alarm-path latency drill")
        col.addView(TextView(this).apply {
            caption()
            text = "Measured 71 ms on Time 2. Re-run on new phones; results " +
                "land in the logs and the server event feed."
        })
        col.addView(Button(this).apply {
            text = "Run latency drill"
            setOnClickListener {
                startService(Intent(this@DebugActivity, MonitorService::class.java)
                    .setAction(MonitorService.ACTION_LATENCY_DRILL))
                Toast.makeText(this@DebugActivity,
                    "Watchapp opens, closes, relaunches with a buzz in ~13 s.",
                    Toast.LENGTH_LONG).show()
            }
        })

        header("S4 — guided HR sensor lab (~13 min)")
        col.addView(TextView(this).apply {
            caption()
            text = "Answers: what does the raw HR sensor report when worn, " +
                "still, loose, and off-wrist? The watch runs burst sampling " +
                "and detectors are held — no alarms during the test."
        })
        labInstruction = TextView(this).apply {
            title()
            setPadding(0, Ui.dp(context, 8), 0, Ui.dp(context, 8))
            text = "Not running."
        }
        col.addView(labInstruction)
        labLive = TextView(this).apply { caption(); text = "" }
        col.addView(labLive)
        labBtn = Button(this).apply {
            text = "Start sensor lab"
            setOnClickListener { if (labRunning) abortLab() else startLab() }
        }
        col.addView(labBtn)
        shareBtn = Button(this).apply {
            text = "Share last lab results"
            visibility = android.view.View.GONE
            setOnClickListener { shareResults() }
        }
        col.addView(shareBtn)

        header("S5 — worker DataLogging liveness (passive)")
        s5Line = TextView(this).apply { caption() }
        col.addView(s5Line)

        header("S6 — battery drain (passive)")
        s6Line = TextView(this).apply { caption() }
        col.addView(s6Line)

        header("S7 — PebbleKit2 end-to-end")
        col.addView(TextView(this).apply {
            caption()
            text = "PASS (field-proven transport). Every latency drill is " +
                "also an automated S7 regression: it exercises the full PK2 " +
                "round trip and records the result per phone model."
        })

        val root = ScrollView(this).apply { addView(col) }
        Ui.applySystemInsets(root)
        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        if (Build.VERSION.SDK_INT >= 33)
            registerReceiver(sampleReceiver,
                IntentFilter(MonitorService.ACTION_HR_SAMPLE),
                Context.RECEIVER_NOT_EXPORTED)
        else @Suppress("UnspecifiedRegisterReceiverFlag")
            registerReceiver(sampleReceiver,
                IntentFilter(MonitorService.ACTION_HR_SAMPLE))
        refreshPassiveCards()
        labInstruction.postDelayed(ticker, 1000)
    }

    override fun onPause() {
        super.onPause()
        runCatching { unregisterReceiver(sampleReceiver) }
        labInstruction.removeCallbacks(ticker)
    }

    override fun onDestroy() {
        if (labRunning) stopLabOnWatch()
        super.onDestroy()
    }

    // ---- lab engine ----

    private val ticker = object : Runnable {
        override fun run() {
            if (labRunning) {
                val left = ((stageEndsAt - System.currentTimeMillis()) / 1000)
                    .coerceAtLeast(0)
                if (left == 0L) advanceStage()
                else updateLabHeader(left)
            }
            refreshPassiveCards()
            labInstruction.postDelayed(this, 1000)
        }
    }

    private fun startLab() {
        labRunning = true
        stageIdx = -1
        csv.clear()
        csv.append("t_rel_s,stage,bpm,event_age_s,heap_bytes\n")
        stageSamples.clear(); stageAges.clear()
        minHeap = Int.MAX_VALUE
        labStartedAt = System.currentTimeMillis()
        labBtn.text = "Abort lab"
        shareBtn.visibility = android.view.View.GONE
        startService(Intent(this, MonitorService::class.java)
            .setAction(MonitorService.ACTION_HR_LAB).putExtra("on", true))
        labInstruction.text = "Starting sensor lab on the watch…\n" +
            "(watchapp opens by itself)"
        // Give the watchapp time to open and the worker to switch to burst.
        stageEndsAt = System.currentTimeMillis() + 8_000
        stageIdx = -1
    }

    private fun advanceStage() {
        stageIdx++
        if (stageIdx >= stages.size) { finishLab(); return }
        val st = stages[stageIdx]
        stageEndsAt = System.currentTimeMillis() + st.seconds * 1000L
        buzz()
        updateLabHeader(st.seconds.toLong())
    }

    private fun updateLabHeader(left: Long) {
        val st = stages.getOrNull(stageIdx)
        labInstruction.text = if (st == null)
            "Starting sensor lab on the watch…" else
            "Stage ${stageIdx + 1}/${stages.size} — ${left}s left\n${st.instruction}"
    }

    private fun onLabSample(bpm: Int, age: Int, heap: Int) {
        if (!labRunning) return
        if (heap in 1 until minHeap) minHeap = heap
        val st = stages.getOrNull(stageIdx) ?: return
        val tRel = (System.currentTimeMillis() - labStartedAt) / 1000
        csv.append("$tRel,${st.key},$bpm,$age,$heap\n")
        stageSamples.getOrPut(st.key) { mutableListOf() }.add(bpm)
        stageAges.getOrPut(st.key) { mutableListOf() }.add(age)
        labLive.text = "live: bpm $bpm · event age ${age}s · worker heap ${heap}B"
    }

    private fun finishLab() {
        stopLabOnWatch()
        labRunning = false
        labBtn.text = "Start sensor lab"
        buzz(); buzz()
        val summary = buildSummary()
        val dir = getExternalFilesDir("logs") ?: File(filesDir, "logs")
        dir.mkdirs()
        val f = File(dir,
            "s4-hr-lab-${SimpleDateFormat("yyyyMMdd-HHmmss",
                Locale.US).format(Date())}.csv")
        runCatching { f.writeText(summary + "\n" + csv.toString()) }
        lastResultFile = f
        labInstruction.text = "DONE — summary:\n$summary"
        shareBtn.visibility = android.view.View.VISIBLE
        CmLog.i("S4Lab", "lab complete, ${csv.lines().size} lines -> ${f.name}")
    }

    private fun abortLab() {
        stopLabOnWatch()
        labRunning = false
        labBtn.text = "Start sensor lab"
        labInstruction.text = "Aborted."
    }

    private fun stopLabOnWatch() {
        startService(Intent(this, MonitorService::class.java)
            .setAction(MonitorService.ACTION_HR_LAB).putExtra("on", false))
    }

    private fun buildSummary(): String {
        val sb = StringBuilder("# S4 HR sensor lab ${Date()}\n")
        sb.append("# model=${Build.MODEL} minWorkerHeap=${minHeap}B\n")
        for (st in stages) {
            val s = stageSamples[st.key] ?: emptyList()
            val nz = s.filter { it > 0 }
            val ages = stageAges[st.key] ?: emptyList()
            sb.append("# ${st.key}: n=${s.size} " +
                "nonzero=${nz.size} (${if (s.isEmpty()) 0
                    else nz.size * 100 / s.size}%) " +
                (if (nz.isEmpty()) "bpm=-" else
                    "bpm=${nz.min()}..${nz.sorted()[nz.size / 2]}..${nz.max()}") +
                " medianEventAge=${if (ages.isEmpty()) "-" else
                    "${ages.sorted()[ages.size / 2]}s"}\n")
        }
        return sb.toString()
    }

    private fun shareResults() {
        val f = lastResultFile ?: return
        val text = runCatching { f.readText() }.getOrElse { "no data" }
        startActivity(Intent.createChooser(Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_SUBJECT, f.name)
            putExtra(Intent.EXTRA_TEXT, text)
        }, "Share S4 lab results"))
    }

    private fun buzz() {
        runCatching {
            @Suppress("DEPRECATION")
            (getSystemService(VIBRATOR_SERVICE) as Vibrator)
                .vibrate(VibrationEffect.createOneShot(400,
                    VibrationEffect.DEFAULT_AMPLITUDE))
        }
    }

    // ---- passive cards ----

    private fun refreshPassiveCards() {
        s5Line.text = if (MonitorService.s5RecordCount == 0)
            "No worker DataLogging records yet. Keep the watchapp CLOSED " +
            "and wear the watch ≥1 h. No records after that = the Core app " +
            "does not forward DataLogging (S5 NO-GO, fallback planned)."
        else {
            val age = (System.currentTimeMillis() -
                MonitorService.s5LastRecT) / 1000
            val verdict = if (MonitorService.s5MedianFlushS in 0..59)
                "→ GO (gate <60 s)" else "→ over the 60 s gate so far"
            "records=${MonitorService.s5RecordCount} · median flush=" +
                "${MonitorService.s5MedianFlushS}s · last ${age}s ago $verdict"
        }

        val pts = getSharedPreferences("batt_hist", MODE_PRIVATE)
            .getString("points", "")!!.split(';').filter { it.isNotEmpty() }
            .mapNotNull { p ->
                p.split(':').takeIf { it.size == 2 }?.let {
                    (it[0].toLongOrNull() ?: return@mapNotNull null) to
                        (it[1].toIntOrNull() ?: return@mapNotNull null)
                }
            }
        s6Line.text = if (pts.size < 2)
            "Collecting watch-battery points (have ${pts.size}; need a " +
            "charge-free day of wear). Also on the dashboard battery trail."
        else {
            val spanH = (pts.last().first - pts.first().first) / 3600.0
            val drop = pts.first().second - pts.last().second
            if (spanH < 1 || drop <= 0)
                "${pts.size} points over ${"%.1f".format(spanH)} h — need " +
                "a longer charge-free stretch for a drain estimate."
            else {
                val perH = drop / spanH
                "${"%.2f".format(perH)} %/h over ${"%.1f".format(spanH)} h " +
                    "→ ${"%.1f".format(100 / perH / 24)} days projected " +
                    if (100 / perH / 24 >= 7) "→ GO (gate ≥7 days)"
                    else "→ under the 7-day gate"
            }
        }
    }
}
