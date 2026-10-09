Puerto a Windows del clásico Styx de DOS, hecho en una tarde con Win32 API pura (sin SDL, sin frameworks, sin dependencias raras). Un solo archivo .c de ~1000 líneas. Porque sí.

text
     ███████ ████████ ██    ██ ██   ██
     ██         ██     ██  ██   ██ ██
     ███████    ██      ████     ███
          ██    ██     ██  ██    ██
     ███████    ██    ██    ██   ██
              2  0  2  6
🕹️ ¿Qué es esto?
El mítico Styx (ese juego donde eres un puntero que dibuja líneas para conquistar territorio mientras un palo de 8 segmentos intenta matarte) reconstruido desde cero en C11 con la Win32 API. Sin SDL2. Sin librerías de terceros. Solo windows.h, gdi32, user32, winmm y mucho cariño.

La lógica está portada del Styx original de DOS (desensamblado incluido), con su Styx de 8 segmentos, sus Sparx que patrullan los bordes, su mecha que se consume, y sus rebotes con reintentos que hacen que el palo se acorte cuando se estrella.

✨ Características
🎨 Framebuffer RGB32 renderizado a mano con SetDIBitsToDevice (nada de GDI lento).

🧠 Lógica fiel al original: 1 Styx en niveles 1-2, 2 desde el nivel 3; el Styx #2 muere si queda atrapado en territorio conquistado.

⚡ Sparx que siguen la pared, nacen dormidas, despiertan cuando te alejas, y van el doble de rápido en niveles >2 cuando solo queda un Styx.

🔥 Mecha: si dibujas y te quedas quieto, explotas. Como debe ser.

🎵 Música y efectos por MIDI con winmm (sí, MIDI en 2026, y suena bien).

📊 HUD con puntos, nivel, territorio, vidas, cercanía del Styx y mecha.

🖥️ Un solo archivo: styx.c. Copiar, pegar, compilar, jugar.

🎮 Controles
Tecla	Acción
Flechas / WASD	Mover
SHIFT + dirección	Dibujar línea (suelta SHIFT y se cancela)
M	Activar/silenciar música
ESPACIO / ENTER	Empezar / reiniciar
ESC	Salir
🏗️ Compilar
Con w64devkit (o MinGW-w64 con GCC):

bash
gcc -std=c11 -O2 styx.c -o styx.exe -mwindows -lgdi32 -luser32 -lwinmm -lm
⚠️ Ojo: hace falta -lwinmm para la música MIDI. Si lo olvidas, el Styx arranca pero sin banda sonora, lo cual es triste.

🎯 Cómo se juega
El objetivo es conquistar el 65% del territorio (80% para bonus de puntos). Sales de un borde de la zona no conquistada, dibujas una línea hacia otra parte del borde, y todo lo que quede encerrado se rellena.

El Styx rebota por la pantalla. Si toca tu línea mientras dibujas, pierdes una vida.

Las Sparx patrullan los bordes. Si te tocan, pierdes una vida.

Si la mecha llega al máximo, pierdes una vida.

Si el trazo supera las 4000 celdas… también pierdes. (El original cortaba a 2000, aquí somos más generosos.)

Con 0 vidas: GAME OVER, y a empezar de nuevo como los valientes.

🧩 ¿Por qué?
Porque hacer un juego completo en Win32 puro, sin dependencias, con MIDI y todo, en pleno 2026, es exactamente el tipo de cosa que uno hace un domingo aburrido. Y porque el Styx original se merecía un port decente.

📜 Licencia
Haz lo que quieras con él. Es un homenaje. Si te sirve, si lo mejoras, si lo portas a otra cosa — adelante. El Styx original no es mío, esto es solo una reimplementación cariñosa.

Hecho en una tarde con demasiado café y una obsesión repentina con el desensamblado. ☕🪵
