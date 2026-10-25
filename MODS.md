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

| Zombi | Base | Desde | Qué hace |
|---|---|---|---|
| Zombi veloz | Zombi | 3-2 | Rojo, camina 2,5 veces más rápido |
| Zombi de hielo | Caracono | 3-4 | Celeste, inmune al frío y a la congelación, 1,5 veces más vida |
| Zombi fantasma | Zombi | 3-7 | Translúcido: los disparos rectos lo atraviesan; solo le hacen daño las catapultas, el fuego en área y las explosiones |
| Caracubo gigante | Caracubo | 3-9 | 1,5 veces más grande, el triple de vida y más lento |

**Cómo están hechos:** cada uno es una variante de un original y comparte su código, a través de `ModContent::BaseSeedType` y `ModContent::BaseZombieType`. Sus valores van al final de `SeedType` y `ZombieType`, así que los originales conservan los suyos. Los nombres y textos del almanaque están en `src/Mods/ModContent.cpp`.

**Sprites:** las dos nueces usan imágenes recoloreadas que genera `scripts/make-mod-art.py` a partir de tus datos del juego. No se guardan en el repo porque derivan del arte de EA. El juego las busca en `<ejecutable>/mods` o en `Contents/Resources/mods`, y `make-macos-app.sh` las genera solo. Si faltan, se usa un tinte.

**Probarlo sin ratón y sin sonido:**

```sh
cp -R ~/Library/Application\ Support/io.github.wszqkzqk/PvZPortable/userdata /tmp/pvz-test/   # copia: no toques el guardado real
SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy PVZ_MOD_TEST=1 PVZ_MOD_TEST_SHOTS=/tmp/shots ./build/pvz-portable -resdir=... -savedir=/tmp/pvz-test
```

- `PVZ_MOD_TEST=1` entra en un nivel, planta las plantas nuevas junto a un lanzaguisantes normal y suelta tres de cada zombi nuevo.
- Cada 2 s escribe en el log los zombis (tipo, fila, posición, velocidad, vida, frío y escala) y cuántos proyectiles hay de cada tipo.
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
