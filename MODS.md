# Mods y trucos (rama `feat/mods`)

## Menú de trucos

En el juego, la tecla a la izquierda del **1** (`º` en el teclado español, `` ` `` en el inglés) o **fn+F1** abre y cierra el menú. Los cambios se guardan solos en el registro del juego y siguen activos al volver a abrirlo.

- **Sol y plantas:** sol infinito, +1000 de sol, sin recarga, precio de las plantas (0–100 %), plantar sin reglas (gratis, sin recarga y mejoras en cualquier casilla) y plantas invencibles.
- **Combate:** daño de las plantas (10–2000 %), matar de un golpe, vida de los zombis nuevos (10–1000 %), zombis congelados, matar todos los zombis e invocar cualquier zombi.
- **Partida:** velocidad x1–x10, dinero infinito ($999,990), ganar el nivel y elegir nivel.

En código: los ajustes están en `src/Mods/Mods.{h,cpp}` y el menú en `src/Mods/HackMenu.cpp` (Dear ImGui v1.93.0, en `third_party/imgui`). Los enganches con el juego son de una o dos líneas, dentro de `#ifdef PVZ_HACK_MENU`. Para desactivar todo: `-DPVZ_HACK_MENU=OFF`.

## Plantas y zombis nuevos (rama `feat/mod-content`)

Están en la página **Mods** del selector de semillas: botón junto a «¡A jugar!». Se desbloquean con la segunda aventura y fuera de la aventura están siempre.

| Planta | Base | Sol | Qué hace |
|---|---|---|---|
| Lanzafuego | Lanzaguisantes | 175 | Dispara bolas de fuego (40 de daño, en área) |
| Hielonuez | Nuez | 100 | Aguanta como una nuez y ralentiza al zombi que la muerde |
| Nuez tiradora | Nuez | 150 | Aguanta como una nuez y dispara guisantes |
| Girasol dorado | Girasol | 100 | Produce dos soles cada vez |
| Lanzaveneno | Lanzaguisantes | 150 | Morado; sus guisantes envenenan 4 s (10 de daño cada medio segundo) |
| Nuez espinosa | Nuez | 125 | Morada; el zombi que la muerde pierde 50 de vida por segundo |
| Girasol sanador | Girasol | 75 | Rosa; no da sol: cada 2,5 s cura 80 a las plantas del 3×3 que lo rodea |
| Cereza de hielo | Petacereza | 125 | Azul; explota con 90 de daño y congela a los zombis de alrededor |

| Zombi | Base | Desde | Qué hace |
|---|---|---|---|
| Zombi veloz | Zombi | 3-2 | Rojo, camina 2,5 veces más rápido |
| Zombi de hielo | Caracono | 3-4 | Celeste, inmune al frío y a la congelación, 1,5 veces más vida |
| Zombi fantasma | Zombi | 3-7 | Translúcido: los disparos rectos lo atraviesan; solo le hacen daño las catapultas, el fuego en área y las explosiones |
| Caracubo gigante | Caracubo | 3-9 | 1,5 veces más grande, el triple de vida y más lento |
| Zombi saltacarril | Zombi | 3-1 | Rosa; mientras camina cambia de fila cada 6 s, como si oliera ajo |
| Zombi divisor | Zombi | 3-3 | Amarillo, 400 de vida; al morir se parte en dos minizombis rápidos |
| Zombi médico | Zombi | 3-5 | Verde; cada 3 s cura 60 a los zombis cercanos (destellan en blanco) |
| Zombi kamikaze | Zombi | 3-6 | Brilla cada vez más; al morir explota y quita 600 a las plantas de alrededor |

Las habilidades al morir (divisor, kamikaze) saltan en `Zombie::DropLoot`, que el juego llama en toda muerte con botín, antes de comprobar si el nivel está ganado: así los minizombis evitan que el nivel termine. El veneno, las curaciones y los cambios de fila van en `ModContent::UpdateZombie` y `ModContent::UpdatePlant`, que se llaman una vez por tic.

## Modos de juego

En **Minijuegos**, botón **Mods** abajo a la derecha. Siempre desbloqueados; los cinco primeros traen 30 oleadas.

| Modo | Escenario | Qué tiene |
|---|---|---|
| Invasión mod | Día | Todos los zombis mod mezclados con los normales |
| Noche fantasma | Noche | Fantasmas, veloces y zombis del periódico |
| Gigantes | Día | Cada zombi 1,4 veces más grande, con el doble de vida y algo más lento |
| Cinta mod | Día | Sin elegir semillas: la cinta trae plantas mod y algunas normales |
| Caos en la piscina | Piscina | Los zombis de la segunda tanda, con flotadores y delfines |
| Versus Día, Noche, Piscina, Niebla y Tejado | Uno cada uno | Dos jugadores: uno pone plantas y el otro manda los zombis (ver abajo) |

### Versus

Cinco modos, uno por escenario, en la segunda fila de la página Mods. No hay oleadas: cada zombi lo manda el jugador zombi, que paga con **cerebros**. Las reglas, los números y las listas de zombis están arriba de todo en `src/Mods/Versus.{h,cpp}`.

**La partida** dura 8 minutos en tres fases, con un letrero al cambiar:

| Fase | Desde | Cerebros cada 3 s | Qué se desbloquea |
|---|---|---|---|
| 1. Avanzada | 0:00 (los primeros 20 s no se manda nada) | 25 | Los zombis ligeros |
| 2. Asedio | 2:24 | 35 | Fútbol, Zomboni, globo, minero, catapulta, la zombotánica fuerte y casi todos los mod |
| 3. Ola final | 5:36 | 50 | Los pesados: Gargantúa, Ojos rojos, Gigante, Bailarín, Nuez alta y la Oleada |

El jugador zombi empieza con 75 cerebros. Cada zombi tiene su propia recarga, como los sobres de semillas.

**Objetivos.** En cada carril hay un cerebro junto a la casa. Primero actúa la podadora, como siempre; cuando un zombi llega a la casa por un carril sin podadora, se come el cerebro de ese carril (o el más cercano que quede) y desaparece. Los zombis ganan al comerse **3 cerebros**; las plantas, si el reloj llega a cero antes. Además:

- Cada planta que se comen los zombis le da al jugador zombi un cuarto de su precio en cerebros (entre 10 y 50).
- Matar un zombi de 150 cerebros o más suelta sol para las plantas (un cuarto de su precio, en soles de 25).
- **Noche:** con T el jugador zombi cambia a «desde la tumba»: el zombi sale de la tumba más cercana a la casa de su carril, por 1,5 veces el precio y con 8 s entre una y otra. Solo los zombis que caminan (normales, cono, cubo, periódico, puerta, fútbol y los mod). Cada tumba que las plantas rompen con la Grave Buster les da 75 de sol.
- **La Oleada** (el abanderado) trae además un zombi en cada uno de los otros carriles.
- **Puenting** se lleva una planta del carril elegido.

Al final sale un letrero con quién ganó y el resumen: zombis mandados, zombis matados, plantas comidas, cerebros comidos y tiempo. Si ganan las plantas, cae el trofeo; si ganan los zombis, el resumen sale en el cuadro de fin de partida, con «Try again» para la revancha.

**Escenarios y zombis.** Cada uno trae los zombis que el juego usa en ese tipo de nivel, los mod y unos pesados para el final, en páginas de hasta nueve:

| Escenario | Páginas | Qué tiene de especial |
|---|---|---|
| Día | Clásicos, Zombotánica, Pesados, Mod | Los zombis con cabeza de planta de «Guisantes en guerra» |
| Noche | Clásicos, Pesados, Mod | Tumbas, sin sol del cielo (las plantas empiezan con 150); el Bailarín |
| Piscina | Clásicos, Piscina, Pesados, Mod | En los dos carriles de agua solo entran los que nadan: Normal, Cono y Cubo (con flotador), la Oleada, el Buceador y el Delfín; los demás salen apagados con una X. El Bobsleigh necesita el hielo de un Zomboni |
| Niebla | Clásicos, Niebla, Pesados, Mod | Niebla desde la columna 6, agua como en la piscina (el Globo vuela por encima), sin sol del cielo (empiezan con 200); caja sorpresa, globo, minero, saltarín |
| Tejado | Clásicos, Tejado, Pesados, Mod | Macetas en las tres primeras columnas, sin minero; puenting, escalera, catapulta, diablillo y Gargantúa de ojos rojos |

- **Clásicos:** Normal, Cono, Cubo, Pértiga, Periódico y Puerta (según el escenario) y la Oleada.
- **Pesados:** Fútbol, Gargantúa (Ojos rojos en el tejado), Gigante (el caracubo gigante) y el Bailarín de noche.
- **Mod:** Veloz, Saltacarril, Fantasma, Hielo, Divisor, Kamikaze y Médico.
- **No salen solos:** el bailarín de apoyo, el zombi con flotador y el minizombi llegan con otro (el Bailarín, cualquier zombi que entra al agua y el Divisor). Tampoco están el Yeti, que se va solo a los 15-20 s y suelta diamantes, ni el Dr. Zomboss, que necesita su propio escenario y su guion.

**Teclas del jugador zombi:** W/S o las flechas eligen el carril (resaltado a la derecha), Q/E cambian de página, 1-9 mandan el zombi de esa casilla y T, de noche, alterna la tumba. La barra de abajo muestra los cerebros, el carril, los sobres de la página (retrato, nombre, precio y recarga; «F2»/«F3» si aún no toca, «X» si no puede ir por ese carril), la fase, el reloj y los cerebros comidos.

**Trucos.** Mientras dura un versus se ignoran todos los trucos que inclinan la partida (sol infinito, sin recarga, precio, plantar sin reglas, plantas invencibles, daño, matar de un golpe, vida de los zombis y zombis congelados) y los botones de un solo uso; solo cuenta la velocidad. El ajuste guardado no cambia. Si alguno está encendido, la partida avisa «Trucos en pausa en Versus».

- **En el mismo equipo:** las plantas con el ratón; los zombis con el teclado.
- **Desde otro aparato:** mientras haya una partida versus, el juego sirve una página en el puerto 8090 (`src/Mods/VersusNet.cpp`). La dirección sale arriba de la barra del juego y lleva un código de 6 letras; sin él no deja pasar. La página muestra el juego en vivo (JPEG, unas 11 imágenes por segundo) y debajo todos los zombis del escenario, agrupados por página, con su precio y su recarga; los que no se pueden mandar salen apagados con el motivo. Los carriles se eligen tocando su número (azul: agua; tachado: ya sin cerebro; con punto morado: tumba). Basta un navegador: otra Mac, un Android o un iPhone.
  - `/state?c=<código>` devuelve el estado en JSON (cerebros, fase, reloj, carril, cerebros comidos, cada zombi con su precio y recarga, y el resumen al final).
  - `/cmd?c=<código>&k=<tecla>` manda una tecla, `&z=<n>` el zombi n de la lista y `&l=<fila>` elige el carril.
- **Desde otra red:** si **Tailscale Funnel** está activado en el tailnet, el juego publica además `https://<equipo>.<tailnet>.ts.net/?c=<código>` (`tailscale funnel --bg 8090`) y lo apaga al salir del versus. `PVZ_VERSUS_FUNNEL=0` lo desactiva. En Android no hay Funnel: el teléfono se alcanza por su dirección local o la de Tailscale.
- La partida versus no se guarda a medias.

**Probarlo sin ratón y sin sonido:**

```sh
cp -R ~/Library/Application\ Support/io.github.wszqkzqk/PvZPortable/userdata /tmp/pvz-test/   # copia: no toques el guardado real
SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy PVZ_MOD_TEST=1 PVZ_MOD_TEST_SHOTS=/tmp/shots ./build/pvz-portable -resdir=... -savedir=/tmp/pvz-test
```

- `PVZ_MOD_TEST=1` entra en un nivel, planta las plantas nuevas junto a un lanzaguisantes normal y suelta tres de cada zombi nuevo.
- Cada 2 s escribe en el log los zombis (tipo, fila, posición, velocidad, vida, frío, congelación, veneno y escala), cuántos proyectiles hay de cada tipo y cuántas plantas siguen vivas y heridas.
- `=modes` abre la página Mods de los minijuegos y `=mode1`…`=mode5` juega ese modo y suelta dos de cada zombi que permite.
- `=mode6`…`=mode10` son los versus de Día, Noche, Piscina, Niebla y Tejado. Con `PVZ_MOD_TEST_VERSUS=1` la prueba planta una defensa válida para el escenario (nenúfares en el agua, macetas en el tejado, setas de noche) y juega el lado zombi con las mismas teclas (y no hace caso al teclado de verdad): recorre la lista página a página, ahorra para cada zombi, busca un carril donde pueda ir y de noche saca uno de cada cinco desde una tumba.
  - `PVZ_MOD_TEST_VERSUS_DEFENSE=debil|normal|fuerte`: con la débil ganan los zombis; con la normal se suelen comer algún cerebro y la partida llega a la ola final; con la fuerte aguantan las plantas.
  - `PVZ_MOD_TEST_VERSUS_SECONDS` acorta la partida (las fases mantienen su parte), `PVZ_MOD_TEST_SPEED=<1-10>` la juega más rápido y `PVZ_MOD_TEST_VERSUS_SHOTS=<ticks>` guarda una captura cada tantos tics. La primera vez que manda de cada página guarda `versus-<escenario>-page<n>.png`.
  - `PVZ_MOD_TEST_CHEATS=1` enciende en memoria los trucos que el versus debe ignorar, para comprobar que los zombis no salen congelados.
  - `PVZ_VERSUS_CODE` fija el código para probar la página con curl (`/stream`, `/state`, `/cmd?k=49`, `/cmd?z=0`, `/cmd?l=1`).
- `PVZ_TEST_HIDDEN_WINDOW=1` crea la ventana oculta (en la Mac no hay contexto GL sin ventana) y `SDL_MAC_BACKGROUND_APP=1` evita que la prueba le quite el foco a quien esté usando la Mac.
- `=chooser` se queda en la página Mods del selector.
- `PVZ_MOD_TEST_SHOTS` hace que el propio juego guarde capturas, así nunca se captura el escritorio.

## Ver en AR (rama `feat/ar-view`)

El jardín sale como un tablero apoyado en una mesa de verdad, visto con la cámara del celular (Chrome en Android, WebXR), y se juega tocándolo. El juego sigue corriendo en la Mac: el celular solo lo muestra y le manda los toques. No hay app ni ARCore en el APK.

- **Encenderlo:** en el menú de trucos, sección **Ver en AR**, casilla «Ver en AR desde el celular» (se guarda como los demás trucos), o `PVZ_AR_VIEW=1`. Funciona en cualquier pantalla, menús incluidos, en el puerto **8092** (`PVZ_AR_PORT`) con un código propio de 6 letras. El código se guarda en el registro, así que el marcador del celular sirve de una vez para otra (`PVZ_AR_CODE` lo fija). Mientras nadie mira, la dirección y el código salen abajo en la pantalla del juego y en el menú de trucos; el celular no los ve.
- **Con cable (lo más simple):** `adb reverse tcp:8092 tcp:8092` y en Chrome del celular `http://localhost:8092/ar?c=<código>`. WebXR solo funciona con https o en localhost, y por el cable la página cuenta como localhost.
- **Sin cable:** con Tailscale en la Mac y en el celular, `tailscale serve --bg 8092` publica `https://<mac>.<tailnet>.ts.net/ar?c=<código>` con certificado válido, solo dentro del tailnet. Por la IP de la red local (`http://192.168…:8092/ar?c=…`) la página carga, pero sin AR: se juega en plano.
- **La página:** primero muestra el juego en plano, que ya se juega con toques o con el ratón. Si el navegador tiene WebXR AR aparece **Entrar en AR**: se apunta a la mesa hasta que sale un círculo y un toque pone el tablero (40 cm de ancho, con marco de madera, el pasto un poco levantado y sombra). En **Ajustar** se arrastra con el dedo por la mesa, ▲▼ lo inclinan de 0° (acostado) a 60° (como una pantalla), «Girar 90» lo gira y −/+ cambian el tamaño. **Fijar** pasa a jugar: cada toque en el tablero es un clic en el juego (botón abajo, movimiento y arriba, así que también se puede arrastrar). **Recolocar** vuelve al círculo para ponerlo en otra superficie y **Salir** cierra la AR.
- three.js 0.160.0 se carga de jsDelivr, así que el celular necesita internet aunque vaya por cable. Sin WebXR (escritorio, iPhone) la página se queda en plano con un aviso.
- **Cómo está hecho:** `src/Mods/ARView.cpp` es un servidor propio, una copia recortada del de `VersusNet.cpp` (que no cambia), con su propia captura: `/ar` (la página, en `src/Mods/ARViewPage.h`), `/stream` (MJPEG, unas 16 imágenes por segundo), `/frame.jpg` (una sola imagen, por si el navegador no puede leer el stream) y `/click?a=down|up|move|tap&x=..&y=..` en coordenadas del juego (800×600). La página lee el stream con `fetch`, lo pinta en un canvas y ese canvas es la textura del tablero. Los clics esperan en una cola y entran en `SexyAppBase::ProcessDeferredMessages`, uno por llamada como los eventos de SDL, directo al `WidgetManager`. Un toque del celular cuenta como si la ventana tuviera el foco, porque los botones no responden sin él y el jugador no está tocando la Mac.
- **Probarlo sin teléfono:** `PVZ_AR_VIEW=1 PVZ_AR_PORT=8093 PVZ_AR_CODE=prueba` y después `curl "localhost:8093/click?c=prueba&a=tap&x=400&y=300"` y `curl -o f.jpg "localhost:8093/frame.jpg?c=prueba"` (la primera vez da 503 y arranca la captura). Con `PVZ_MOD_TEST_SHOTS` el juego guarda una captura 0,7 s después de cada toque (`ar-tapNN-XxY.png`).

## Gráficos, sonidos y textos

```sh
python3 scripts/extract-pak.py "<carpeta de datos>/main.pak" ~/Library/Application\ Support/io.github.wszqkzqk/PvZPortable/gamedata-mod
```

Esto saca los 3198 archivos de `main.pak`: `images/`, `sounds/`, `reanim/`, `particles/` y `properties/LawnStrings.txt`, que tiene todos los textos. Si la carpeta `gamedata-mod` existe, la app la usa en vez de `main.pak`. Edita ahí y vuelve a abrir el juego. Para volver al original, borra o renombra la carpeta.

- **Imágenes y sonidos** (`images/`, `sounds/`): se usan tal cual, sin caché.
- **Animaciones y partículas** (`reanim/*.reanim`, `particles/*.xml`): el juego carga primero su versión compilada. Si editas una, borra su `.compiled` en `gamedata-mod/compiled/` y en `cache64/compiled/`, en la carpeta de datos del juego; si no, el cambio no se ve.

## Partida guardada

Está en `~/Library/Application Support/io.github.wszqkzqk/PvZPortable/userdata/`. Los primeros 16 bytes de `user1.dat` son cuatro uint32 en little-endian: versión (12), nivel de la aventura (1–50; 21 es el 3-1), dinero/10 (tope 99999 = $999,990) y aventuras terminadas.

Edítalo con el juego cerrado y después de copiar `userdata/`. Si cambias de nivel, aparta `game1_*.v4` (la partida a medias).

## Compilar e instalar en la Mac

```sh
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
scripts/make-macos-app.sh dist ~/Library/Application\ Support/io.github.wszqkzqk/PvZPortable/gamedata
ditto "dist/Plants vs. Zombies.app" "/Applications/Plants vs. Zombies.app"
```

Los datos del juego (`main.pak` + `properties/`) salen de la copia de Steam: en la consola de Steam, `download_depot 3590 3591` (la versión Windows, 51 MB). Los datos no van en el repo.

## Android (rama `feat/android`)

```sh
ANDROID_NDK=/ruta/al/ndk ANDROID_HOME=~/Library/Android/sdk scripts/make-android-apk.sh /tmp/pvz-android
```

Sin vcpkg: el script baja y compila con el NDK SDL2, libpng, libjpeg-turbo y libopenmpt (con sus decodificadores incluidos, `android/openmpt/`), luego `libmain.so`, y Gradle arma `dist/pvz-portable-android-arm64-v8a.apk` firmado con la llave de depuración. `JOBS` (2 por defecto) limita la compilación en paralelo. Necesita la plataforma 36 del SDK y un JDK 17 o más nuevo.

- **Menú de trucos:** tres dedos a la vez en la pantalla lo abren y lo cierran (también la X de la ventana). Se escala con la densidad de la pantalla; arrastrar el cuerpo de la ventana la desplaza. Lo que se toca encima del menú no le llega al juego.
- **Sprites de las nueces:** la carpeta `mods/` va junto a `main.pak` en el ZIP o la carpeta de datos que se importa; termina en `Android/data/io.github.wszqkzqk.pvzportable/files/mods/`, donde el juego la busca.
- **Los dos ZIP desde la Mac:** `scripts/make-android-import-zips.py <carpeta>` arma `PvZ-datos.zip` (`main.pak`, `properties/` y los `mods/` de la app instalada) y `PvZ-partida.zip` (la partida de la Mac), y comprueba que el importador deja cada archivo en su sitio.
- **Partida:** un ZIP con `userdata/users.dat`, `userdata/user1.dat` y, si se quiere seguir el nivel a medias, `userdata/game1_0.v4`. Se importa desde «Manage Data» (mantener pulsado el icono). `registry.regemu` no se importa, así que los trucos empiezan apagados.
