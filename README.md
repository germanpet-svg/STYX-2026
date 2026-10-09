# Styx for Windows

**Un port del clásico Styx de DOS, reconstruido en C11 con la API Win32 nativa.**  
Sin SDL, sin frameworks y sin dependencias externas: un único archivo C y las bibliotecas estándar de Windows.

> Un homenaje hecho por curiosidad, café y una obsesión repentina con el desensamblado. ☕🕹️

```text
     ███████ ████████ ██    ██ ██   ██
     ██         ██     ██  ██   ██ ██
     ███████    ██      ████     ███
          ██    ██     ██  ██    ██
     ███████    ██    ██    ██   ██
              2  0  2  6
```

## 🎮 Acerca del juego

Styx es un juego arcade de conquista territorial: trazas líneas para capturar áreas de la pantalla mientras evitas a los enemigos que patrullan y rebotan por el campo de juego.

Esta versión es una reimplementación para Windows escrita en **C11**. Busca conservar elementos característicos del juego original de DOS, como el Styx segmentado, las Sparx que recorren los bordes y el riesgo de que la mecha se agote mientras dibujas.

## ✨ Características

- **Win32 nativo:** utiliza la API de Windows, sin SDL2 ni frameworks de terceros.
- **Renderizado RGB32:** framebuffer propio presentado mediante `SetDIBitsToDevice`.
- **Enemigos Styx:** uno en los niveles 1–2 y dos a partir del nivel 3.
- **Comportamiento de enemigos:** incluye rebotes, reintentos y reducción del Styx cuando se estrella, según la lógica implementada.
- **Sparx:** patrullan los bordes, comienzan dormidas y se activan cuando te alejas; en niveles superiores a 2 pueden duplicar su velocidad cuando queda un solo Styx.
- **Mecha:** el tiempo durante el trazado es limitado; si se agota, pierdes una vida.
- **Música y efectos MIDI:** gestionados mediante `winmm`.
- **HUD:** muestra puntuación, nivel, territorio, vidas, proximidad del Styx y estado de la mecha.
- **Código compacto:** toda la implementación se concentra en `styx.c`.

## 🕹️ Controles

| Tecla | Acción |
|---|---|
| `← ↑ → ↓` / `W A S D` | Moverse |
| `Shift` + dirección | Dibujar una línea |
| Soltar `Shift` | Cancelar el trazado |
| `M` | Activar o silenciar la música |
| `Espacio` / `Enter` | Empezar o reiniciar |
| `Esc` | Salir |

## 🏗️ Compilación

### Requisitos

- Windows.
- [w64devkit](https://github.com/skeeto/w64devkit) o MinGW-w64 con GCC.
- El archivo fuente `styx.c`.

### Compilar con GCC

Abre una terminal en el directorio que contiene `styx.c` y ejecuta:

```bash
gcc -std=c11 -O2 styx.c -o styx.exe -mwindows -lgdi32 -luser32 -lwinmm -lm
```

Después, ejecuta `styx.exe`.

**Importante:** `-lwinmm` enlaza la biblioteca multimedia de Windows necesaria para la música MIDI.

## 🎯 Cómo jugar

1. Empieza desde el borde de la zona que todavía no has conquistado.
2. Muévete hacia otra parte del borde para trazar una línea y cerrar un área.
3. El área capturada se incorpora al territorio conquistado.
4. Evita que el Styx toque tu línea mientras estás trazando y mantente lejos de las Sparx.
5. Vigila la mecha: si se agota durante el trazado, pierdes una vida.

### Objetivos y peligros

- **Objetivo principal:** conquistar el **65 %** del territorio.
- **Bonus:** alcanzar el **80 %** para obtener puntos adicionales.
- Si el Styx toca tu línea durante el trazado, pierdes una vida.
- Si una Sparx te alcanza, pierdes una vida.
- Si la mecha llega al límite, pierdes una vida.
- Los trazos de más de **4.000 celdas** también provocan la pérdida de una vida.
- Al quedarte sin vidas, aparece `GAME OVER` y tendrás que empezar otra partida.

## 🔧 Tecnologías

- Lenguaje: **C11**
- Plataforma: **Windows**
- API gráfica y de ventanas: **Win32**, `gdi32`, `user32`
- Música y efectos: **winmm**
- Compilador recomendado: **GCC con w64devkit o MinGW-w64**

## 📁 Estructura

```text
.
├── styx.c
└── README.md
```

La estructura anterior describe la distribución mínima esperada para compilar el juego; si el repositorio incluye otros archivos, consúltalos directamente en el proyecto.

## 📜 Licencia y atribución

Este proyecto es una **reimplementación no oficial y hecha por aficionados**, inspirada en Styx para DOS. No se afirma la autoría del juego original ni de sus elementos originales. Los derechos sobre el juego original pertenecen a sus respectivos titulares.

El código de esta reimplementación se ofrece con intención de homenaje. **No se ha especificado una licencia formal para este repositorio**; antes de reutilizar, modificar o redistribuir el código, conviene definir una licencia explícita y comprobar los derechos aplicables al material original.

---

**Hecho en 2026 con Win32, C11 y muchas ganas de volver a jugar.**
