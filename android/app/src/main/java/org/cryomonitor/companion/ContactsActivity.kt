package org.cryomonitor.companion

import android.app.AlertDialog
import android.content.Intent
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import android.text.InputType
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TextView
import android.widget.Toast
import org.cryomonitor.companion.Ui.title
import kotlin.concurrent.thread

/**
 * The wearer's safety net: escalation order (tiers with timing), contact
 * list with add/edit/delete, self-notification address, and the fire
 * drill one tap away from editing (spec: verification adjacent to
 * configuration). Server is the source of truth — every edit is an API
 * call with inline field errors.
 */
class ContactsActivity : AppCompatActivity() {

    private lateinit var settings: SettingsStore
    private lateinit var client: ServerClient
    private lateinit var content: LinearLayout
    private var payload: ServerClient.ContactsPayload? = null
    private var selfStatus: ServerClient.WearerStatus? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        settings = SettingsStore(this)
        client = ServerClient(settings)

        content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(Ui.dp(context, 16), Ui.dp(context, 16), Ui.dp(context, 16), Ui.dp(context, 16))
        }
        val root = ScrollView(this).apply { addView(content) }
        Ui.applySystemInsets(root)
        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun refresh() {
        thread {
            payload = client.fetchContacts()
            selfStatus = client.fetchStatus()
            runOnUiThread { render() }
        }
    }

    // ---- rendering ----

    private fun render() {
        content.removeAllViews()
        val p = payload
        if (p == null) {
            content.addView(TextView(this).apply {
                text = "Cannot load contacts (${client.lastResult}).\n" +
                    "Check the server connection in Settings."
            })
            content.addView(Button(this).apply {
                text = "Retry"; setOnClickListener { refresh() }
            })
            return
        }

        if (selfStatus?.degraded == true) {
            content.addView(banner("⚠ ALARMS REACH NOBODY\nAdd at least one " +
                "contact with a working channel below."))
        }

        header("Your safety net")
        val tiers = p.tiers.sortedBy { it.position }
        tiers.forEachIndexed { idx, tier ->
            val members = p.contacts.filter { it.tierName == tier.name }
            val timing = if (idx == 0) "alerted immediately"
            else "added if nobody acknowledges within " +
                "${tiers[idx - 1].promoteAfterS / 60} min"
            val names = if (members.isEmpty()) "— nobody —"
            else members.joinToString(", ") { it.name }
            content.addView(TextView(this).apply {
                text = "Tier ${idx + 1} '${tier.name}': $names\n" +
                    "   $timing; unacknowledged contacts re-alerted every " +
                    "${tier.repeatAfterS / 60} min"
                setPadding(0, Ui.dp(context, 4), 0, Ui.dp(context, 8))
            })
        }

        header("Contacts")
        p.contacts.forEach { c ->
            val channels = listOfNotNull(
                c.telegramChatId?.let { "telegram" },
                c.ntfyTopic?.let { "ntfy" },
                c.email?.let { "email" }).joinToString(", ")
            content.addView(Button(this).apply {
                text = "${c.name}  ·  $channels  ·  tier '${c.tierName}'"
                setOnClickListener { editContactDialog(c) }
            })
        }
        content.addView(Button(this).apply {
            text = "+ Add contact"
            setOnClickListener {
                editContactDialog(ServerClient.Contact(
                    null, "", p.tiers.firstOrNull()?.name ?: "primary",
                    null, null, null))
            }
        })

        header("Copies to yourself")
        content.addView(TextView(this).apply {
            text = "Optional: get a copy on your own Telegram/ntfy/email " +
                "whenever your contacts are alerted — useful on a second " +
                "device when this phone is dead. Copies have no " +
                "acknowledge button."
        })
        content.addView(Button(this).apply {
            text = "Edit self-notification"
            setOnClickListener { selfNotifyDialog() }
        })

        header("Verify")
        content.addView(Button(this).apply {
            text = "🔔 Fire drill — send a TEST alert now"
            setOnClickListener {
                startService(Intent(this@ContactsActivity,
                    MonitorService::class.java)
                    .setAction(MonitorService.ACTION_TEST_ALARM))
                Toast.makeText(this@ContactsActivity,
                    "TEST alarm sent — contacts get messages tagged [TEST]",
                    Toast.LENGTH_LONG).show()
            }
        })
    }

    private fun header(text: String) = content.addView(TextView(this).apply {
        this.text = text
        title()
        setPadding(0, Ui.dp(context, 24), 0, Ui.dp(context, 8))
    })

    private fun banner(text: String) = TextView(this).apply {
        this.text = text
        setBackgroundColor(Ui.errorContainer(this))
        setTextColor(Ui.onErrorContainer(this))
        title()
        setPadding(Ui.dp(context, 16), Ui.dp(context, 16),
                   Ui.dp(context, 16), Ui.dp(context, 16))
    }

    // ---- contact editor ----

    private fun editContactDialog(contact: ServerClient.Contact) {
        val p = payload ?: return
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(Ui.dp(context, 24), Ui.dp(context, 12), Ui.dp(context, 24), Ui.dp(context, 4))
        }
        fun field(label: String, value: String?, type: Int): EditText {
            col.addView(TextView(this).apply { text = label })
            val e = EditText(this).apply { setText(value ?: ""); inputType = type }
            col.addView(e)
            return e
        }
        val fName = field("Name", contact.name, InputType.TYPE_CLASS_TEXT)
        val fTg = field("Telegram chat id (message the bot to get it)",
                        contact.telegramChatId, InputType.TYPE_CLASS_PHONE)
        val fNtfy = field("ntfy topic", contact.ntfyTopic,
                          InputType.TYPE_CLASS_TEXT)
        val fEmail = field("Email", contact.email,
                           InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS)

        col.addView(TextView(this).apply { text = "Tier" })
        val spinner = Spinner(this)
        val tierNames = p.tiers.sortedBy { it.position }.map { it.name }
        spinner.adapter = ArrayAdapter(this,
            android.R.layout.simple_spinner_dropdown_item, tierNames)
        spinner.setSelection(maxOf(0, tierNames.indexOf(contact.tierName)))
        col.addView(spinner)

        val dialog = AlertDialog.Builder(this)
            .setTitle(if (contact.id == null) "Add contact" else "Edit contact")
            .setView(ScrollView(this).apply { addView(col) })
            .setPositiveButton("Save", null)   // set below to control dismiss
            .setNegativeButton("Cancel", null)
            .apply {
                if (contact.id != null) setNeutralButton("Delete") { _, _ ->
                    confirmDelete(contact)
                }
            }
            .create()
        dialog.show()

        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
            val candidate = ServerClient.Contact(
                id = contact.id,
                name = fName.text.toString().trim(),
                tierName = spinner.selectedItem as String,
                telegramChatId = fTg.text.toString().trim().ifEmpty { null },
                ntfyTopic = fNtfy.text.toString().trim().ifEmpty { null },
                email = fEmail.text.toString().trim().ifEmpty { null })

            // client-side floor: the server re-validates everything
            if (candidate.name.isEmpty()) {
                fName.error = "required"; return@setOnClickListener
            }
            if (candidate.telegramChatId == null && candidate.ntfyTopic == null
                && candidate.email == null) {
                fTg.error = "at least one channel"; return@setOnClickListener
            }
            thread {
                val result = client.saveContact(candidate)
                runOnUiThread {
                    when (result) {
                        is ServerClient.SaveResult.Ok -> {
                            dialog.dismiss(); refresh()
                        }
                        is ServerClient.SaveResult.FieldErrors -> {
                            result.fields["name"]?.let { fName.error = it }
                            result.fields["telegram_chat_id"]?.let { fTg.error = it }
                            result.fields["ntfy_topic"]?.let { fNtfy.error = it }
                            result.fields["email"]?.let { fEmail.error = it }
                            result.fields["channels"]?.let { fTg.error = it }
                            result.fields["tier_name"]?.let {
                                Toast.makeText(this, "Tier: $it",
                                    Toast.LENGTH_LONG).show()
                            }
                        }
                        is ServerClient.SaveResult.Failed ->
                            Toast.makeText(this, "Save failed: ${result.why}",
                                Toast.LENGTH_LONG).show()
                    }
                }
            }
        }
    }

    private fun confirmDelete(contact: ServerClient.Contact) {
        AlertDialog.Builder(this)
            .setTitle("Remove ${contact.name}?")
            .setMessage("They will no longer be alerted for any alarm.")
            .setPositiveButton("Remove") { _, _ ->
                thread {
                    val ok = contact.id != null && client.deleteContact(contact.id)
                    runOnUiThread {
                        if (!ok) Toast.makeText(this, "Delete failed",
                            Toast.LENGTH_LONG).show()
                        refresh()
                    }
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    // ---- self-notify ----

    private fun selfNotifyDialog() {
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(Ui.dp(context, 24), Ui.dp(context, 12), Ui.dp(context, 24), Ui.dp(context, 4))
        }
        fun field(label: String): EditText {
            col.addView(TextView(this).apply { text = label })
            val e = EditText(this)
            col.addView(e)
            return e
        }
        val fTg = field("Your Telegram chat id")
        val fNtfy = field("Your ntfy topic")
        val fEmail = field("Your email")
        col.addView(TextView(this).apply {
            text = "Leave all empty to turn copies off."
            setPadding(0, Ui.dp(context, 8), 0, 0)
        })

        val dialog = AlertDialog.Builder(this)
            .setTitle("Copies of alerts to yourself")
            .setView(ScrollView(this).apply { addView(col) })
            .setPositiveButton("Save", null)
            .setNegativeButton("Cancel", null)
            .create()
        dialog.show()
        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener {
            thread {
                val result = client.setSelfNotify(
                    fTg.text.toString().trim().ifEmpty { null },
                    fNtfy.text.toString().trim().ifEmpty { null },
                    fEmail.text.toString().trim().ifEmpty { null })
                runOnUiThread {
                    when (result) {
                        is ServerClient.SaveResult.Ok -> {
                            Toast.makeText(this, "Saved",
                                Toast.LENGTH_SHORT).show()
                            dialog.dismiss()
                        }
                        is ServerClient.SaveResult.FieldErrors -> {
                            result.fields["telegram_chat_id"]?.let { fTg.error = it }
                            result.fields["ntfy_topic"]?.let { fNtfy.error = it }
                            result.fields["email"]?.let { fEmail.error = it }
                        }
                        is ServerClient.SaveResult.Failed ->
                            Toast.makeText(this, "Save failed: ${result.why}",
                                Toast.LENGTH_LONG).show()
                    }
                }
            }
        }
    }
}
