package dev.zat.benchdeck

import android.content.Context
import org.json.JSONObject
import java.io.File

/** 結果JSONのうち画面に出す部分 */
data class ResultSummary(
    val file: File?,
    val preset: String,
    val score: Long?,
    val scoreNote: String,
    val avgFps: Double,
    val low1Fps: Double,
    val gpuMs: Double?,
    val cpuMs: Double?,
    val thermalMax: String,
    val validLaps: Int,
    val laps: Int,
    val aborted: Boolean,
    val abortReason: String,
    val sections: List<Pair<String, Double>>,
    val device: String,
    val timingMethod: String,
    val timestamp: Long,
) {
    companion object {
        fun parse(json: String, file: File? = null, timestamp: Long = 0L): ResultSummary? = runCatching {
            val o = JSONObject(json)
            val summary = o.getJSONObject("summary")
            val secs = o.optJSONObject("sections")
            val sections = mutableListOf<Pair<String, Double>>()
            secs?.keys()?.forEach { k -> sections += k to secs.getJSONObject(k).optDouble("avg_fps", 0.0) }
            ResultSummary(
                file = file,
                preset = o.optString("preset"),
                score = if (o.isNull("score")) null else o.getLong("score"),
                scoreNote = o.optString("score_note"),
                avgFps = summary.optDouble("avg_fps", 0.0),
                low1Fps = summary.optDouble("low1_fps", 0.0),
                gpuMs = if (summary.isNull("gpu_ms_avg")) null else summary.getDouble("gpu_ms_avg"),
                cpuMs = if (summary.isNull("cpu_ms_avg")) null else summary.getDouble("cpu_ms_avg"),
                thermalMax = o.optString("thermal_max"),
                validLaps = o.optInt("valid_laps"),
                laps = o.optJSONArray("laps")?.length() ?: 0,
                aborted = o.optBoolean("aborted"),
                abortReason = o.optString("abort_reason"),
                sections = sections,
                device = o.optJSONObject("device")?.optString("name").orEmpty(),
                timingMethod = o.optString("timing_method"),
                timestamp = timestamp,
            )
        }.getOrNull()
    }
}

/** 結果はアプリ内部ストレージ（filesDir/results）に保存し、共有インテントで書き出す */
class ResultStore(context: Context) {
    private val dir = File(context.filesDir, "results").apply { mkdirs() }

    fun save(preset: String, json: String, csv: String): Pair<File, File> {
        val stamp = System.currentTimeMillis()
        val base = "benchdeck_${preset}_$stamp"
        val j = File(dir, "$base.json").apply { writeText(json) }
        val c = File(dir, "$base.csv").apply { writeText(csv) }
        return j to c
    }

    fun list(limit: Int = 30): List<ResultSummary> =
        dir.listFiles { f -> f.extension == "json" }.orEmpty()
            .sortedByDescending { it.lastModified() }
            .take(limit)
            .mapNotNull { f -> ResultSummary.parse(f.readText(), f, f.lastModified()) }

    fun csvFor(json: File): File = File(json.parentFile, json.nameWithoutExtension + ".csv")
}
