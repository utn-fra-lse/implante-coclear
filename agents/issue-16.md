### Descripción

Como ultima fase, se busca poder seleccionar entre distintas frecuencias de muestreo del ADC del tx (mic-dsp), el objetivo principal es para verificar la calidad de audio y el tiempo de adquisicion a distintas frecuencias.

Preliminarmente se van a realizar saltos de 8kHz hasta llegar a 32kHz o 16kHz hasta llegar a 64kHz, esto debe prefijarse debido a que se tieen que cargar los coeficientes de los filtros a la frecuencia de muestreo deseada.

### Criterios de aceptación

- [ ] Ajustar el script para que se pueda modificar la frecuencia del ADC, manteniendo el flujo de adquisicion y procesamiento.
- [ ] Calcular y cargar los coeficientes de los distintos niveles
- [ ] Armar la posibilidad de variar la frecuencia de muestreo de forma dinamica por medio de UART/USB

### Contexto técnico / referencias

_No response_

### Prioridad

Alta

### Estimación (horas o story points)

2h