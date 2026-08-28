# Independent-review prompts for the two upstream PRs

Copy each prompt (everything inside its fence) into any capable AI
model (Codex, GPT, etc.). The diffs are embedded, so the reviewer
needs no repository access. Paste whatever findings come back into the
Claude session for triage.

---

## Prompt 1 — PebbleOS firmware fix (26 lines)

```
You are an adversarial code reviewer for a firmware pull request to
PebbleOS (the open-source Pebble smartwatch OS, C, FreeRTOS-based).
Your job is to find real problems, not to praise.

CONTEXT
The Pebble Time 2 heart-rate driver (Goodix GH3x2x) reports events with
bpm=0 and quality=HRMQuality_OffWrist when the watch is not worn. In
src/fw/services/activity/activity.c, prv_hrm_subscription_cb() rejects
bpm 0 via a validity range check, so
activity_metrics_prv_add_median_hr_sample() — the only writer of
state->hr.metrics.current_bpm — never runs off-wrist. That field backs
the public API health_service_peek_current_value(
HealthMetricHeartRateRawBPM), which therefore serves the last on-wrist
reading indefinitely (hardware-confirmed: a watch on a table returned
the identical stale bpm for 9+ minutes while events kept arriving at
~1 Hz). The patch invalidates the stored metric on off-wrist events.
It was compiled for the obelix (Time 2) board and field-tested on real
hardware: after the fix, the peeked value drops to 0 within ~30 s of
removal, and worn behavior is unchanged across a 450-sample guided
test.

The state struct is guarded by mutex_lock_recursive/unlock in all
existing accessors. current_bpm and current_quality are
ActivityScalarStore (integer); current_update_time_utc is uint32.
HRMQuality_OffWrist is an enum used elsewhere in the same file.

REVIEW TASKS
1. Correctness: any way this change corrupts state, deadlocks (the
   caller prv_hrm_subscription_cb runs on the kernel background task
   and already calls other mutex-taking helpers), or races?
2. Semantics: could zeroing the raw metric break OTHER in-tree
   consumers of ActivityMetricHeartRateRawBPM / RawQuality /
   RawUpdatedTimeUTC (e.g. the system Health app showing "last known
   HR"), and is that acceptable for an API whose event payload already
   reports 0 off-wrist?
3. API contract: is serving 0 for "no signal" defensible for a metric
   documented as the unfiltered raw reading, versus alternatives
   (serving the stale value with a stale timestamp; a new "invalid"
   sentinel)?
4. Style: naming/placement consistent with the described conventions
   (helper beside activity_metrics_prv_set_hrm_worn_status, doc
   comment in the header)?
5. Anything a firmware maintainer would push back on?

OUTPUT: a numbered list of findings, each with severity
(blocker/concern/nit), the exact lines involved, and a suggested fix.
If you find nothing in a category, say so explicitly.

THE COMPLETE DIFF:
diff --git a/include/pbl/services/activity/activity_private.h b/include/pbl/services/activity/activity_private.h
index b1b29a9..09263af 100644
--- a/include/pbl/services/activity/activity_private.h
+++ b/include/pbl/services/activity/activity_private.h
@@ -531,6 +531,12 @@ void activity_metrics_prv_add_median_hr_sample(PebbleHRMEvent *hrm_event, time_t
 //! @param[in] is_offwrist true if the event's HRMQuality was HRMQuality_OffWrist
 void activity_metrics_prv_set_hrm_worn_status(time_t now_utc, bool is_offwrist);
 
+//! Invalidate the current (peekable) raw HR reading because the HRM reports
+//! the watch is off-wrist: ActivityMetricHeartRateRawBPM reads 0 until a valid
+//! on-wrist reading arrives. Called once per off-wrist BPM event.
+//! @param[in] now_utc current UTC time
+void activity_metrics_prv_set_raw_hr_offwrist(time_t now_utc);
+
 //! Returns true if the HRM has recently reported the watch is off-wrist. The most recent BPM
 //! event must have been HRMQuality_OffWrist and must have arrived within the last
 //! ACTIVITY_HRM_OFFWRIST_STALE_SEC seconds, otherwise this returns false.
diff --git a/src/fw/services/activity/activity.c b/src/fw/services/activity/activity.c
index 5cf77ce..423e92e 100644
--- a/src/fw/services/activity/activity.c
+++ b/src/fw/services/activity/activity.c
@@ -188,6 +188,14 @@ T_STATIC void prv_hrm_subscription_cb(PebbleHRMEvent *hrm_event, void *context)
     activity_metrics_prv_set_hrm_worn_status(
         now_utc, hrm_event->bpm.quality == HRMQuality_OffWrist);
 
+    if (hrm_event->bpm.quality == HRMQuality_OffWrist) {
+      // Off-wrist events carry bpm 0, which fails the validity check, so the
+      // peekable raw HR metric was never updated and kept serving the last
+      // on-wrist reading indefinitely. Invalidate it so
+      // HealthMetricHeartRateRawBPM reads 0, matching the event payload.
+      activity_metrics_prv_set_raw_hr_offwrist(now_utc);
+    }
+
     if (valid_hr_reading) {
       // Update the heart rate metrics
       activity_metrics_prv_add_median_hr_sample(hrm_event, now_utc, now_uptime_ts);
diff --git a/src/fw/services/activity/activity_metrics.c b/src/fw/services/activity/activity_metrics.c
index a75afae..c299323 100644
--- a/src/fw/services/activity/activity_metrics.c
+++ b/src/fw/services/activity/activity_metrics.c
@@ -611,6 +611,18 @@ void activity_metrics_prv_set_hrm_worn_status(time_t now_utc, bool is_offwrist)
   mutex_unlock_recursive(state->mutex);
 }
 
+// --------------------------------------------------------------------------------------------
+void activity_metrics_prv_set_raw_hr_offwrist(time_t now_utc) {
+  ActivityState *state = activity_private_state();
+  mutex_lock_recursive(state->mutex);
+  {
+    state->hr.metrics.current_bpm = 0;
+    state->hr.metrics.current_quality = HRMQuality_OffWrist;
+    state->hr.metrics.current_update_time_utc = now_utc;
+  }
+  mutex_unlock_recursive(state->mutex);
+}
+
 // --------------------------------------------------------------------------------------------
 bool activity_metrics_prv_is_hrm_offwrist(time_t now_utc) {
   ActivityState *state = activity_private_state();
```

---

## Prompt 2 — Core mobile app DataLogging forwarding (92 lines)

```
You are an adversarial code reviewer for a Kotlin Multiplatform pull
request to the open-source Pebble companion app (Android/iOS, Koin DI,
modules commonMain/androidMain/iosMain/jvmMain).

CONTEXT
Pebble watchapps log data via "DataLogging"; background workers on the
watch have no other channel to the phone. The app's
Datalogging.logData() handled health tags and system-app tags and
silently dropped all third-party data (while the protocol layer ACKs
everything to the watch). The patch adds a CompanionDatalogging
interface, forwards non-health non-system items to it, and implements
it on Android by broadcasting the classic PebbleKit intent
com.getpebble.action.dl.RECEIVE_DATA once per item (Base64 payload),
mirroring the app's existing PebbleKitClassic AppMessage compatibility
which uses sendOrderedBroadcast the same way. iOS/JVM bind a no-op.
Field-tested on real hardware: a watch worker logging one 14-byte
record per minute delivered records to a companion receiver through
this build (zero delivered before the patch).

Known deliberate trade-offs (already documented in the PR): timestamp
and tag ride as `long` extras (original PebbleKit receivers cast these
to Guava UnsignedInteger and will skip the records — same as their
current behavior of receiving nothing; the repo has no Guava
dependency); delivery is fire-and-forget (companion ACK_DATA ignored,
no phone-side buffering).

REVIEW TASKS
1. Correctness: threading (onDataItems is called from a protocol
   coroutine), the item-splitting loop (offset/size arithmetic,
   partial trailing items), dataId monotonicity across sessions and
   process restarts, and the UUID/session synthesis.
2. Android correctness: implicit ordered broadcasts on modern Android
   (delivery to runtime- vs manifest-registered receivers), payload
   size limits for broadcast extras given itemSize up to several KB,
   battery/spam concerns at high logging rates.
3. Security/privacy: the broadcast is unrestricted — any installed app
   can register for com.getpebble.action.dl.RECEIVE_DATA and read any
   watchapp's logged data. The original PebbleKit protocol had the
   same property. Is preserving it acceptable, or should delivery be
   restricted (e.g. resolve the companion package like PK2 does and
   setPackage() the intent)? Rate this one carefully.
4. KMP/DI hygiene: interface placement in commonMain, per-platform
   bindings, constructor injection into Datalogging.
5. Anything an app maintainer would push back on?

OUTPUT: a numbered list of findings, each with severity
(blocker/concern/nit), the exact lines involved, and a suggested fix.
If you find nothing in a category, say so explicitly.

THE COMPLETE DIFF:
diff --git a/libpebble3/src/androidMain/kotlin/io/rebble/libpebblecommon/di/LibPebbleModule.android.kt b/libpebble3/src/androidMain/kotlin/io/rebble/libpebblecommon/di/LibPebbleModule.android.kt
index 63b5984f..2ef31cf2 100644
--- a/libpebble3/src/androidMain/kotlin/io/rebble/libpebblecommon/di/LibPebbleModule.android.kt
+++ b/libpebble3/src/androidMain/kotlin/io/rebble/libpebblecommon/di/LibPebbleModule.android.kt
@@ -18,6 +18,8 @@ import io.rebble.libpebblecommon.connection.bt.classic.pebble.BtClassicConnector
 import io.rebble.libpebblecommon.connection.endpointmanager.timeline.AndroidNotificationActionHandler
 import io.rebble.libpebblecommon.connection.endpointmanager.timeline.PlatformNotificationActionHandler
 import io.rebble.libpebblecommon.contacts.SystemContacts
+import io.rebble.libpebblecommon.datalogging.CompanionDatalogging
+import io.rebble.libpebblecommon.pebblekit.classic.PebbleKitClassicDatalogging
 import io.rebble.libpebblecommon.io.rebble.libpebblecommon.calls.AndroidPhoneReceiver
 import io.rebble.libpebblecommon.io.rebble.libpebblecommon.calls.AndroidSystemCallLog
 import io.rebble.libpebblecommon.connection.bt.classic.transport.AndroidClassicScanner
@@ -76,6 +78,7 @@ actual val platformModule: Module = module {
     singleOf(::AndroidSystemContacts) bind SystemContacts::class
     singleOf(::AndroidPhoneReceiver) bind LegacyPhoneReceiver::class
     singleOf(::NotificationCallDetector)
+    singleOf(::PebbleKitClassicDatalogging) bind CompanionDatalogging::class
     single { get<AppContext>().context }
     single { get<AppContext>().context as Application }
     single { NotificationHandler(setOf(get<BasicNotificationProcessor>()), get(), get(), get(), get(), get(), get(), get(), get(), get()) }
diff --git a/libpebble3/src/androidMain/kotlin/io/rebble/libpebblecommon/pebblekit/classic/io/rebble/libpebblecommon/pebblekit/classic/PebbleKitClassicDatalogging.kt b/libpebble3/src/androidMain/kotlin/io/rebble/libpebblecommon/pebblekit/classic/io/rebble/libpebblecommon/pebblekit/classic/PebbleKitClassicDatalogging.kt
new file mode 100644
index 00000000..d2a62f19
--- /dev/null
+++ b/libpebble3/src/androidMain/kotlin/io/rebble/libpebblecommon/pebblekit/classic/io/rebble/libpebblecommon/pebblekit/classic/PebbleKitClassicDatalogging.kt
@@ -0,0 +1,61 @@
+package io.rebble.libpebblecommon.pebblekit.classic
+
+import android.content.Context
+import android.content.Intent
+import android.util.Base64
+import co.touchlab.kermit.Logger
+import io.rebble.libpebblecommon.datalogging.CompanionDatalogging
+import java.util.UUID
+import kotlin.uuid.Uuid
+import kotlin.uuid.toJavaUuid
+
+/**
+ * Delivers third-party datalogging via the classic PebbleKit broadcast
+ * protocol (`com.getpebble.action.dl.RECEIVE_DATA`), one broadcast per
+ * logged item. Timestamp and tag are sent as long extras: receivers built
+ * against the original PebbleKit jar expect Guava UnsignedInteger extras
+ * there and will silently skip these records; receivers parsing primitive
+ * extras receive them.
+ */
+class PebbleKitClassicDatalogging(private val context: Context) : CompanionDatalogging {
+    private var dataId = 1
+
+    override fun onDataItems(appUuid: Uuid, tag: UInt, itemSize: UShort, data: ByteArray) {
+        val size = itemSize.toInt()
+        if (size <= 0 || data.size < size) return
+        val sessionUuid = UUID.nameUUIDFromBytes("$appUuid/$tag".encodeToByteArray())
+        val timestampS = System.currentTimeMillis() / 1000
+        var offset = 0
+        var items = 0
+        while (offset + size <= data.size) {
+            val item = data.copyOfRange(offset, offset + size)
+            val intent = Intent(INTENT_DL_RECEIVE_DATA).apply {
+                putExtra(APP_UUID, appUuid.toJavaUuid())
+                putExtra(DATA_LOG_UUID, sessionUuid)
+                putExtra(DATA_LOG_TIMESTAMP, timestampS)
+                putExtra(DATA_LOG_TAG, tag.toLong())
+                putExtra(PBL_DATA_ID, dataId++)
+                putExtra(PBL_DATA_TYPE, TYPE_BYTES)
+                putExtra(PBL_DATA_OBJECT, Base64.encodeToString(item, Base64.NO_WRAP))
+            }
+            // Ordered broadcasts: regular ones are sometimes delayed on Android 14+.
+            context.sendOrderedBroadcast(intent, null)
+            offset += size
+            items++
+        }
+        logger.d { "Forwarded $items datalogging item(s) for $appUuid tag $tag" }
+    }
+
+    private companion object {
+        private val logger = Logger.withTag("PebbleKitClassicDatalogging")
+        private const val INTENT_DL_RECEIVE_DATA = "com.getpebble.action.dl.RECEIVE_DATA"
+        private const val APP_UUID = "uuid"
+        private const val DATA_LOG_UUID = "data_log_uuid"
+        private const val DATA_LOG_TIMESTAMP = "data_log_timestamp"
+        private const val DATA_LOG_TAG = "data_log_tag"
+        private const val PBL_DATA_ID = "pbl_data_id"
+        private const val PBL_DATA_TYPE = "pbl_data_type"
+        private const val PBL_DATA_OBJECT = "pbl_data_object"
+        private const val TYPE_BYTES: Byte = 0
+    }
+}
diff --git a/libpebble3/src/commonMain/kotlin/io/rebble/libpebblecommon/datalogging/CompanionDatalogging.kt b/libpebble3/src/commonMain/kotlin/io/rebble/libpebblecommon/datalogging/CompanionDatalogging.kt
new file mode 100644
index 00000000..2355654f
--- /dev/null
+++ b/libpebble3/src/commonMain/kotlin/io/rebble/libpebblecommon/datalogging/CompanionDatalogging.kt
@@ -0,0 +1,16 @@
+package io.rebble.libpebblecommon.datalogging
+
+import kotlin.uuid.Uuid
+
+/**
+ * Forwards third-party watchapp datalogging to the app's phone companion.
+ * Background workers cannot use AppMessage, so datalogging is their only
+ * channel to the phone while the watchapp is closed.
+ */
+interface CompanionDatalogging {
+    fun onDataItems(appUuid: Uuid, tag: UInt, itemSize: UShort, data: ByteArray)
+}
+
+object NoOpCompanionDatalogging : CompanionDatalogging {
+    override fun onDataItems(appUuid: Uuid, tag: UInt, itemSize: UShort, data: ByteArray) = Unit
+}
diff --git a/libpebble3/src/commonMain/kotlin/io/rebble/libpebblecommon/datalogging/Datalogging.kt b/libpebble3/src/commonMain/kotlin/io/rebble/libpebblecommon/datalogging/Datalogging.kt
index 2206a960..19183e50 100644
--- a/libpebble3/src/commonMain/kotlin/io/rebble/libpebblecommon/datalogging/Datalogging.kt
+++ b/libpebble3/src/commonMain/kotlin/io/rebble/libpebblecommon/datalogging/Datalogging.kt
@@ -14,6 +14,7 @@ import kotlin.uuid.Uuid
 class Datalogging(
     private val webServices: WebServices,
     private val healthDataProcessor: HealthDataProcessor,
+    private val companionDatalogging: CompanionDatalogging,
 ) {
     private val logger = Logger.withTag("Datalogging")
 
@@ -32,6 +33,12 @@ class Datalogging(
             return
         }
 
+        // Third-party watchapp data belongs to its companion app
+        if (uuid != SYSTEM_APP_UUID) {
+            companionDatalogging.onDataItems(uuid, tag, itemSize, data)
+            return
+        }
+
         // Handle system-app datalogging tags
         if (uuid == SYSTEM_APP_UUID) {
             when (tag) {
diff --git a/libpebble3/src/iosMain/kotlin/io/rebble/libpebblecommon/di/LibPebbleModule.ios.kt b/libpebble3/src/iosMain/kotlin/io/rebble/libpebblecommon/di/LibPebbleModule.ios.kt
index a13644bd..e7d92c2e 100644
--- a/libpebble3/src/iosMain/kotlin/io/rebble/libpebblecommon/di/LibPebbleModule.ios.kt
+++ b/libpebble3/src/iosMain/kotlin/io/rebble/libpebblecommon/di/LibPebbleModule.ios.kt
@@ -1,5 +1,7 @@
 package io.rebble.libpebblecommon.di
 
+import io.rebble.libpebblecommon.datalogging.CompanionDatalogging
+import io.rebble.libpebblecommon.datalogging.NoOpCompanionDatalogging
 import io.rebble.libpebblecommon.calendar.IosCalendarActionHandler
 import io.rebble.libpebblecommon.calendar.IosSystemCalendar
 import io.rebble.libpebblecommon.calendar.PlatformCalendarActionHandler
@@ -38,6 +40,7 @@ import org.koin.dsl.module
 import kotlin.time.Duration.Companion.seconds
 
 actual val platformModule: Module = module {
+    single<CompanionDatalogging> { NoOpCompanionDatalogging }
     single {
         PhoneCapabilities(
             CommonPhoneCapabilities + setOf(
diff --git a/libpebble3/src/jvmMain/kotlin/io/rebble/libpebblecommon/di/LibPebbleModule.jvm.kt b/libpebble3/src/jvmMain/kotlin/io/rebble/libpebblecommon/di/LibPebbleModule.jvm.kt
index 9ec85418..3421600c 100644
--- a/libpebble3/src/jvmMain/kotlin/io/rebble/libpebblecommon/di/LibPebbleModule.jvm.kt
+++ b/libpebble3/src/jvmMain/kotlin/io/rebble/libpebblecommon/di/LibPebbleModule.jvm.kt
@@ -1,5 +1,7 @@
 package io.rebble.libpebblecommon.di
 
+import io.rebble.libpebblecommon.datalogging.CompanionDatalogging
+import io.rebble.libpebblecommon.datalogging.NoOpCompanionDatalogging
 import org.koin.core.module.Module
 
 actual val platformModule: Module
```
