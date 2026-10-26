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
| Versus | Día | Dos jugadores: uno pone plantas y el otro manda los zombis (ver abajo) |

### Versus

No hay oleadas: cada zombi lo manda el jugador zombi, que gasta **cerebros**. Empieza con 50 y gana 25 cada 3 s. Los primeros 15 s no se puede mandar nada, para que las plantas arranquen. Los zombis ganan si uno llega a la casa; las plantas, si aguantan 3 minutos. Las reglas y la lista de zombis (Normal 50, Cono 75, Cubo 125, Pértiga 75, Veloz 75, Divisor 100, Kamikaze 100, Médico 125, Fútbol 175) están en `src/Mods/Versus.cpp`.

- **En el mismo equipo:** las plantas con el ratón; los zombis con W/S o las flechas (carril) y 1-9 (zombi).
- **Desde otro aparato:** mientras haya una partida versus, el juego sirve una página en el puerto 8090 (`src/Mods/VersusNet.cpp`). La dirección sale arriba de la barra del juego y lleva un código de 6 letras; sin él no deja pasar. La página muestra el juego en vivo (JPEG, unas 11 imágenes por segundo) y manda las mismas teclas, con botones para el dedo. Basta un navegador: otra Mac, un Android o un iPhone.
- **Desde otra red:** si **Tailscale Funnel** está activado en el tailnet, el juego publica además `https://<equipo>.<tailnet>.ts.net/?c=<código>` (`tailscale funnel --bg 8090`) y lo apaga al salir del versus. `PVZ_VERSUS_FUNNEL=0` lo desactiva. En Android no hay Funnel: el teléfono se alcanza por su dirección local o la de Tailscale.
- La partida versus no se guarda a medias.

**Cómo están hechos:** cada uno es una variante de un original y comparte su código, a través de `ModContent::BaseSeedType` y `ModContent::BaseZombieType`. Sus valores van al final de `SeedType` y `ZombieType`, así que los originales conservan los suyos. Los nombres y textos del almanaque están en `src/Mods/ModContent.cpp`.

**Sprites:** hay dos maneras de recolorear.

- **Nueces** (Hielonuez, Nuez tiradora, Nuez espinosa): imágenes que genera `scripts/make-mod-art.py` a partir de tus datos del juego. No se guardan en el repo porque derivan del arte de EA. El juego las busca en `<ejecutable>/mods` o en `Contents/Resources/mods`, y `make-macos-app.sh` las genera solo. Si faltan, se usa un tinte.
- **Lanzafuego, Lanzaveneno, Girasol sanador y Cereza de hielo:** filtros que cambian un tono por otro al vuelo (`FilterEffectDoHueMap` en `src/PvzpLib/FilterEffect.cpp`, el mismo sistema que blanquea a la Imitadora). Teñir no basta: una planta verde teñida de morado solo se oscurece. Cada planta tiene su filtro en `ModContent::PlantFilter`, que se aplica a sus animaciones y al dibujo de su sobre. El guisante venenoso usa el mismo filtro morado.

**Probarlo sin ratón y sin sonido:**

```sh
cp -R ~/Library/Application\ Support/io.github.wszqkzqk/PvZPortable/userdata /tmp/pvz-test/   # copia: no toques el guardado real
SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy PVZ_MOD_TEST=1 PVZ_MOD_TEST_SHOTS=/tmp/shots ./build/pvz-portable -resdir=... -savedir=/tmp/pvz-test
```

- `PVZ_MOD_TEST=1` entra en un nivel, planta las plantas nuevas junto a un lanzaguisantes normal y suelta tres de cada zombi nuevo.
- Cada 2 s escribe en el log los zombis (tipo, fila, posición, velocidad, vida, frío, congelación, veneno y escala), cuántos proyectiles hay de cada tipo y cuántas plantas siguen vivas y heridas.
- `=modes` abre la página Mods de los minijuegos y `=mode1`…`=mode5` juega ese modo y suelta dos de cada zombi que permite. `=mode6` es el versus: con `PVZ_MOD_TEST_VERSUS=1` planta una defensa y juega el lado zombi (y no hace caso al teclado de verdad), `PVZ_MOD_TEST_VERSUS_SECONDS` acorta la partida y `PVZ_VERSUS_CODE` fija el código para probar la página con curl (`/stream`, `/cmd?k=49`).
- `=chooser` se queda en la página Mods del selector.
- `PVZ_MOD_TEST_SHOTS` hace que el propio juego guarde capturas, así nunca se captura el escritorio.

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
- **Partida:** un ZIP con `userdata/users.dat`, `userdata/user1.dat` y, si se quiere seguir el nivel a medias, `userdata/game1_0.v4`. Se importa desde «Manage Data» (mantener pulsado el icono). `registry.regemu` no se importa, así que los trucos empiezan apagados.
