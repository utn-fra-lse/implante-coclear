# pico2_rx

Implementación de un algoritmo que captura una trama de datos de 16 bits por ASK. La trama se recibe a una frecuencia de 125 KHz y las especificaciones son:

<div align="center">
<table>
    <thead>
        <tr>
            <th rowspan=2>T [us]</th>
            <th colspan=3>Duty [%]</th>
        </tr>
        <tr>
            <th>Bit 0</th>
            <th>Bit 1</th>
            <th>No Bit</th>
        </tr>
    </thead>
    <tbody>
        <tr>
            <td>8</td>
            <td>25</td>
            <td>75</td>
            <td>50</td>
        </tr>
    </tbody>
</table>
</div>

El algoritmo recibe bit a bit de la trama y arma la original transmitida.

## Pines usados

Los pines que hay que conectar son:

<center>

| GPIO | Uso |
| --- | --- |
| 16 | Entrada de datos para la trama |

</center>