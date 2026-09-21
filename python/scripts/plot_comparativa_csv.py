"""
Grafica el CSV generado por `pico_pc_comparator.py --save-csv`.

Lee el CSV en formato largo (filas "fft" y filas "band") y reproduce el gráfico de
comparación FFT PC vs FFT Pico + banco de filtros de 3 vías que antes generaba
directamente pico_pc_comparator.py.
"""
import argparse
import csv
import os
import re

import numpy as np
import matplotlib.pyplot as plt


def normalize(x):
    peak = np.max(x)
    return x / peak if peak > 1e-12 else x


def read_csv(csv_path):
    freqs, pc_mag, pico_mag = [], [], []
    band_labels, pc_bands, pico_bands_fft, pico_bands_onboard = [], [], [], []

    with open(csv_path, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row["kind"] == "fft":
                freqs.append(float(row["freq_hz"]))
                pc_mag.append(float(row["pc"]))
                pico_mag.append(float(row["pico"]))
            elif row["kind"] == "band":
                band_labels.append(row["band_label"])
                pc_bands.append(float(row["pc"]))
                pico_bands_fft.append(float(row["pico_fft"]))
                pico_bands_onboard.append(float(row["pico_onboard"]))

    return (
        np.array(freqs, dtype=np.float64),
        np.array(pc_mag, dtype=np.float64),
        np.array(pico_mag, dtype=np.float64),
        band_labels,
        np.array(pc_bands, dtype=np.float64),
        np.array(pico_bands_fft, dtype=np.float64),
        np.array(pico_bands_onboard, dtype=np.float64),
    )


def plot_results(freqs, band_labels, pc_mag, pico_mag, pc_bands, pico_bands_fft, pico_bands_onboard, save_path, letra=None):
    fig, (ax_fft, ax_bands) = plt.subplots(2, 1, figsize=(10, 9))

    ax_fft.plot(freqs, normalize(pc_mag), label="PC", color="#1f77b4")
    ax_fft.plot(freqs, normalize(pico_mag), label="Pico", color="#ff8800")
    fft_title = "FFT PC vs FFT Pico (magnitud normalizada por pico, promedio temporal)"
    if letra is not None:
        fft_title += f' — Letra "{letra.upper()}"'
    ax_fft.set_title(fft_title)
    ax_fft.set_xlabel("Frecuencia (Hz)")
    ax_fft.set_ylabel("Magnitud (normalizada)")
    ax_fft.legend()
    ax_fft.grid(alpha=0.3)
    ax_fft.set_ylim(0, 1.08)

    n_bands = len(band_labels)
    x = np.arange(n_bands)
    width = 0.26
    ax_bands.bar(x - width, normalize(pc_bands), width, label="PC", color="#1f77b4")
    ax_bands.bar(x, normalize(pico_bands_fft), width, label="PC/FFT Pico", color="#2ca02c")
    ax_bands.bar(x + width, normalize(pico_bands_onboard), width, label="Pico", color="#ff8800")
    ax_bands.set_title("Banco de filtros: 3 vías (energía normalizada por pico)")
    ax_bands.set_xticks(x)
    ax_bands.set_xticklabels(band_labels, rotation=30, ha="right")
    ax_bands.set_ylabel("Energía (normalizada)")
    ax_bands.legend()
    ax_bands.grid(alpha=0.3, axis="y")
    ax_bands.set_ylim(0, 1.08)

    fig.tight_layout()

    if save_path:
        fig.savefig(save_path, dpi=150)
        print(f"\nGráfico guardado en: {save_path}")
    else:
        plt.show()


def main():
    parser = argparse.ArgumentParser(
        description="Grafica el CSV generado por pico_pc_comparator.py --save-csv"
    )
    parser.add_argument("csv_path", type=str, help="Ruta al CSV generado por pico_pc_comparator.py")
    parser.add_argument("--save", type=str, default=None, help="Guardar el gráfico como PNG en vez de mostrarlo")
    parser.add_argument("--letra", type=str, default=None, help="Letra/sonido grabado, para el título del gráfico (si no se pasa, se intenta detectar del nombre del archivo)")
    args = parser.parse_args()

    letra = args.letra
    if letra is None:
        match = re.search(r"_([A-Za-z])\.csv$", os.path.basename(args.csv_path), re.IGNORECASE)
        if match:
            letra = match.group(1)

    freqs, pc_mag, pico_mag, band_labels, pc_bands, pico_bands_fft, pico_bands_onboard = read_csv(args.csv_path)

    plot_results(freqs, band_labels, pc_mag, pico_mag, pc_bands, pico_bands_fft, pico_bands_onboard, args.save, letra)


if __name__ == "__main__":
    main()
