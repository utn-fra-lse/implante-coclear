import argparse
import numpy as np
from scipy import signal

def print_c_code(sos, name="biquad", out_format="default"):
    """
    Imprime los coeficientes de un filtro en formato biquad (Second-Order Sections) listo para C.
    scipy devuelve el formato [b0, b1, b2, a0, a1, a2] por cada sección.
    Normalmente a0 es 1.0.
    """
    print(f"// Filtro: {name} (Secciones: {len(sos)})")
    
    if out_format == "cmsis-df1":
        print("// Formato CMSIS-DSP (arm_biquad_cascade_df1_f32)")
        print("// NOTA: CMSIS-DSP DF1 usa y[n] = b0*x[n] + ... + a1*y[n-1] + a2*y[n-2]")
        print("// Por lo tanto, los signos de a1 y a2 ESTÁN INVERTIDOS respecto a SciPy.")
        print(f"float {name}_coeffs[{len(sos) * 5}] = {{")
        
        for i, section in enumerate(sos):
            b0, b1, b2, a0, a1, a2 = section
            b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0
            
            # Invertimos signo de a1 y a2 para CMSIS DF1
            print(f"    {b0:.8f}f, {b1:.8f}f, {b2:.8f}f, {-a1:.8f}f, {-a2:.8f}f, // Sección {i+1}")
        print("};")
        print(f"// arm_biquad_casd_df1_inst_f32 S;")
        print(f"// float pState[{len(sos) * 4}];")
        print(f"// arm_biquad_cascade_df1_init_f32(&S, {len(sos)}, {name}_coeffs, pState);")
        
    elif out_format == "cmsis-df2t":
        print("// Formato CMSIS-DSP (arm_biquad_cascade_df2T_f32)")
        print("// NOTA: CMSIS-DSP DF2T usa los coeficientes a1 y a2 IGUAL que SciPy.")
        print(f"float {name}_coeffs[{len(sos) * 5}] = {{")
        for i, section in enumerate(sos):
            b0, b1, b2, a0, a1, a2 = section
            b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0
            
            print(f"    {b0:.8f}f, {b1:.8f}f, {b2:.8f}f, {a1:.8f}f, {a2:.8f}f, // Sección {i+1}")
        print("};")
        print(f"// arm_biquad_cascade_df2T_instance_f32 S;")
        print(f"// float pState[{len(sos) * 2}];")
        print(f"// arm_biquad_cascade_df2T_init_f32(&S, {len(sos)}, {name}_coeffs, pState);")

    else:
        print(f"// Formato genérico de coeficientes: {{b0, b1, b2, a1, a2}} (a0=1.0 implícito)")
        print(f"float {name}[{len(sos)}][5] = {{")
        for i, section in enumerate(sos):
            b0, b1, b2, a0, a1, a2 = section
            b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0
            print(f"    {{{b0:.8f}f, {b1:.8f}f, {b2:.8f}f, {a1:.8f}f, {a2:.8f}f}}, // Sección {i+1}")
        print("};")
    print()

def main():
    parser = argparse.ArgumentParser(description="Calculador de coeficientes Biquad (SOS) con SciPy para C/C++")
    parser.add_argument("--fs", type=float, default=16000, help="Frecuencia de muestreo en Hz (default: 16000)")
    parser.add_argument("--order", type=int, default=2, help="Orden total del filtro (default: 2, equivale a 1 biquad para LP/HP)")
    parser.add_argument("--type", choices=['lowpass', 'highpass', 'bandpass', 'notch'], default='lowpass', help="Tipo de filtro")
    parser.add_argument("--f1", type=float, required=True, help="Frecuencia de corte 1 (Hz)")
    parser.add_argument("--f2", type=float, help="Frecuencia de corte 2 (Hz) (solo para bandpass)")
    parser.add_argument("--q", type=float, default=30.0, help="Factor de calidad (solo para notch, default: 30.0)")
    parser.add_argument("--format", choices=['default', 'cmsis-df1', 'cmsis-df2t'], default='cmsis-df2t', help="Formato de salida (default: cmsis-df2t)")
    
    args = parser.parse_args()
    
    fs = args.fs
    nyq = 0.5 * fs
    
    if args.f1 >= nyq:
        print(f"Error: La frecuencia f1 ({args.f1} Hz) debe ser estrictamente menor que Nyquist ({nyq} Hz).")
        return

    # Generamos los coeficientes usando Second-Order Sections (SOS) 
    # que es computacionalmente muchísimo más estable que usar coeficientes directos de grado mayor.
    if args.type in ['lowpass', 'highpass']:
        # scipy devuelve SOS de tamaño (N_sections, 6)
        sos = signal.butter(args.order, args.f1 / nyq, btype=args.type, output='sos')
        name = f"filter_{args.type}_{int(args.f1)}Hz"
        
    elif args.type == 'bandpass':
        if not args.f2:
            print("Error: Necesitás especificar --f2 para un filtro bandpass.")
            return
        if args.f2 >= nyq:
            print(f"Error: La frecuencia f2 ({args.f2} Hz) debe ser estrictamente menor que Nyquist ({nyq} Hz).")
            return
            
        sos = signal.butter(args.order, [args.f1 / nyq, args.f2 / nyq], btype='bandpass', output='sos')
        name = f"filter_bandpass_{int(args.f1)}_{int(args.f2)}Hz"
        
    elif args.type == 'notch':
        # Notch saca el ruido en una frecuencia exacta.
        w0 = args.f1 / nyq
        b, a = signal.iirnotch(w0, args.q)
        # Formateamos el resultado b y a como una única sección biquad SOS
        sos = np.array([[b[0], b[1], b[2], a[0], a[1], a[2]]])
        name = f"filter_notch_{int(args.f1)}Hz_Q{int(args.q)}"
        
    print_c_code(sos, name, args.format)

if __name__ == "__main__":
    main()
