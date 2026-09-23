import os
import sys
import numpy as np
from scipy import signal

# Definimos cada frecuencia con su factor de oversampling óptimo (respetando el máximo de 500 ksps del ADC de la Pico 2)
PRESET_SPECS = [
    {"fs": 8000,  "oversampling": 16, "adc_clk_hz": 128000},
    {"fs": 16000, "oversampling": 16, "adc_clk_hz": 256000},
    # {"fs": 24000, "oversampling": 16, "adc_clk_hz": 384000},
    {"fs": 32000, "oversampling": 8,  "adc_clk_hz": 256000},
    # {"fs": 48000, "oversampling": 8,  "adc_clk_hz": 384000},
    {"fs": 64000, "oversampling": 4,  "adc_clk_hz": 256000},
]

def calc_cmsis_df1_hpf(fs, fc=250.0, order=4):
    """Calcula coeficientes HPF Butterworth en formato CMSIS DF1 (b0, b1, b2, -a1, -a2)."""
    nyq = 0.5 * fs
    cutoff = min(fc, nyq * 0.45)
    sos = signal.butter(order, cutoff / nyq, btype='highpass', output='sos')
    coeffs = []
    for section in sos:
        b0, b1, b2, a0, a1, a2 = section
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0
        coeffs.extend([b0, b1, b2, -a1, -a2])
    return coeffs

def calc_cmsis_df1_lpf(fs, fc=6000.0, order=2):
    """Calcula coeficientes LPF Butterworth en formato CMSIS DF1 (b0, b1, b2, -a1, -a2)."""
    nyq = 0.5 * fs
    cutoff = min(fc, nyq * 0.45)
    sos = signal.butter(order, cutoff / nyq, btype='lowpass', output='sos')
    coeffs = []
    for section in sos:
        b0, b1, b2, a0, a1, a2 = section
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0
        coeffs.extend([b0, b1, b2, -a1, -a2])
    return coeffs

def generate_header(output_path):
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    
    lines = []
    lines.append("// Autogenerado por python/scripts/generate_dsp_coeffs.py")
    lines.append("// NO EDITAR MANUALMENTE")
    lines.append("#ifndef _FILTER_COEFFS_H_")
    lines.append("#define _FILTER_COEFFS_H_")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append('#include "arm_math.h"')
    lines.append("")
    lines.append("typedef struct {")
    lines.append("    uint32_t sample_rate;")
    lines.append("    uint16_t adc_clk_khz;")
    lines.append("    uint8_t oversampling_factor;")
    lines.append("    const float32_t *hpf_coeffs;")
    lines.append("    const float32_t *lpf_coeffs;")
    lines.append("} dsp_preset_t;")
    lines.append("")
    lines.append(f"#define NUM_DSP_PRESETS {len(PRESET_SPECS)}")
    lines.append("")

    # Definir arrays de coeficientes por cada frecuencia
    for spec in PRESET_SPECS:
        fs = spec["fs"]
        hpf = calc_cmsis_df1_hpf(fs)
        lpf = calc_cmsis_df1_lpf(fs)
        
        lines.append(f"// Coeficientes para Fs = {fs} Hz (Oversampling = {spec['oversampling']}x)")
        lines.append(f"static const float32_t hpf_coeffs_{fs}Hz[10] = {{")
        hpf_str = ", ".join([f"{v:.8f}f" for v in hpf[:5]]) + ",\n    " + ", ".join([f"{v:.8f}f" for v in hpf[5:]])
        lines.append(f"    {hpf_str}")
        lines.append("};")
        
        lines.append(f"static const float32_t lpf_coeffs_{fs}Hz[5] = {{")
        lpf_str = ", ".join([f"{v:.8f}f" for v in lpf])
        lines.append(f"    {lpf_str}")
        lines.append("};")
        lines.append("")

    # Arreglo de presets
    lines.append("static const dsp_preset_t dsp_presets[NUM_DSP_PRESETS] = {")
    for spec in PRESET_SPECS:
        fs = spec["fs"]
        adc_clk_khz = int(spec["adc_clk_hz"] / 1000)
        ovs = spec["oversampling"]
        lines.append(f"    {{ {fs}U, {adc_clk_khz}U, {ovs}U, hpf_coeffs_{fs}Hz, lpf_coeffs_{fs}Hz }},")
    lines.append("};")
    lines.append("")

    # Función helper inline para buscar preset
    lines.append("static inline const dsp_preset_t* dsp_get_preset(uint32_t sample_rate) {")
    lines.append("    for (uint32_t i = 0; i < NUM_DSP_PRESETS; i++) {")
    lines.append("        if (dsp_presets[i].sample_rate == sample_rate) {")
    lines.append("            return &dsp_presets[i];")
    lines.append("        }")
    lines.append("    }")
    lines.append("    return &dsp_presets[1]; // Default a 16000 Hz si no se encuentra")
    lines.append("}")
    lines.append("")
    lines.append("#endif // _FILTER_COEFFS_H_")
    lines.append("")

    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"Generado exitosamente: {output_path}")

if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(script_dir, "..", ".."))
    target_header = os.path.join(repo_root, "rpipico", "pico2_mic_dsp", "dsp", "include", "filter_coeffs.h")
    generate_header(target_header)
