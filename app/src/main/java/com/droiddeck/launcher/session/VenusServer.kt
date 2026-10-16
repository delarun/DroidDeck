package com.droiddeck.launcher.session

import android.content.Context
import android.util.Log
import com.droiddeck.launcher.core.DeviceSupport
import com.droiddeck.launcher.core.FileUtils
import com.droiddeck.launcher.runtime.LinuxRuntime
import java.io.File

object VenusServer {
    private const val TAG = "VenusServer"
    private const val BINARY = "libvenus-server.so"
    private const val SOCKET = "venus.sock"
    const val GUEST_DIR = "/usr/local/lib/droiddeck-venus"
    private const val ICD = "$GUEST_DIR/venus_icd.json"
    private const val LIBRARY = "$GUEST_DIR/libvulkan_virtio.so"

    @Volatile private var process: Process? = null

    @Volatile var active = false
        private set

    fun env(): List<String> = if (!active) emptyList() else listOf(
        "VK_ICD_FILENAMES=$ICD",
        "VK_DRIVER_FILES=$ICD",
        "VN_DEBUG=vtest",
        "DROIDDECK_VENUS=1",
        "VTEST_SOCKET_NAME=${LinuxRuntime.GUEST_RUNTIME_DIR}/$SOCKET",
    )

    @Synchronized
    fun start(context: Context, runtimeDir: File, logDir: File): String {
        stop()
        if (!DeviceSupport.mali()) return "venus: not a Mali GPU"
        val binary = File(context.applicationInfo.nativeLibraryDir, BINARY)
        if (!binary.isFile) return "venus: the app has no $BINARY"
        val root = LinuxRuntime.rootDir(context)
        if (!File(root, LIBRARY.removePrefix("/")).isFile) return "venus: the runtime has no ${LIBRARY.removePrefix("/")}"
        val icd = File(root, ICD.removePrefix("/"))
        val manifest = "{\"file_format_version\": \"1.0.1\", \"ICD\": {\"library_path\": \"$LIBRARY\", \"api_version\": \"1.4.0\"}}\n"
        if (FileUtils.readString(icd) != manifest && !FileUtils.writeString(icd, manifest)) return "venus: cannot write $ICD"
        val socket = File(runtimeDir, SOCKET).apply { delete() }
        return try {
            val started = ProcessBuilder(binary.path, "--venus", "--no-virgl", "--socket-path", socket.path)
                .apply { environment()["VIRGL_LOG_LEVEL"] = "info" }
                .redirectErrorStream(true)
                .redirectOutput(File(logDir, "venus.log"))
                .start()
            val deadline = System.currentTimeMillis() + 3000
            while (!socket.exists() && started.isAlive && System.currentTimeMillis() < deadline) Thread.sleep(20)
            if (!socket.exists()) {
                started.destroy()
                "venus: server did not open its socket (alive=${started.isAlive})"
            } else {
                process = started
                active = true
                "venus: serving the system Vulkan driver on ${socket.path}"
            }
        } catch (e: Exception) {
            Log.w(TAG, "start", e)
            "venus: start failed: ${e.message}"
        }
    }

    @Synchronized
    fun stop() {
        active = false
        process?.destroy()
        process = null
        val me = android.os.Process.myPid()
        File("/proc").listFiles { f -> f.name.all { it.isDigit() } }?.forEach { proc ->
            val pid = proc.name.toIntOrNull() ?: return@forEach
            if (pid == me) return@forEach
            val cmdline = runCatching { File(proc, "cmdline").readText() }.getOrNull() ?: return@forEach
            if (cmdline.substringBefore('\u0000').endsWith("/$BINARY")) android.os.Process.killProcess(pid)
        }
    }
}
