package com.droiddeck.launcher.core

import android.content.Context
import android.os.Build
import com.droiddeck.launcher.R
import com.droiddeck.launcher.gpu.GpuInfo
import java.io.File

object DeviceSupport {
    fun adreno(): Boolean =
        File("/sys/class/kgsl/kgsl-3d0").exists() || File("/vendor/lib64/hw/vulkan.adreno.so").exists()

    fun mali(): Boolean =
        !adreno() && (File("/dev/mali0").exists() || GpuInfo.systemProperty("ro.hardware.vulkan").equals("mali", ignoreCase = true))

    fun vendorVulkan(): Boolean = !adreno() && GpuInfo.systemProperty("ro.hardware.vulkan").let { it.isNotEmpty() && !it.equals("adreno", ignoreCase = true) }

    fun systemVulkanIcd(): String? {
        val names = listOfNotNull(GpuInfo.systemProperty("ro.hardware.vulkan").ifEmpty { null }, "adreno", "mali").distinct()
        return names.map { "/vendor/lib64/hw/vulkan.$it.so" }.firstOrNull { File(it).exists() }
    }

    fun gpuName(context: Context): String {
        val soc = if (Build.VERSION.SDK_INT >= 31) Build.SOC_MODEL.takeIf { it.isNotBlank() && it != Build.UNKNOWN } else null
        return soc?.let { "$it (${Build.HARDWARE})" } ?: Build.HARDWARE.ifBlank { context.getString(R.string.gpu_this_gpu) }
    }

    fun lowLatencyWifiSafe(): Boolean = Build.VERSION.SDK_INT < 31 || Build.SOC_MODEL != "CQ8725S"
}
