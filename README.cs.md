# OKNO

[English](README.md) · **Česky**

*Pamatuješ si jen to, co se vejde do okna.*

Logická hra o paměti pro Nintendo 3DS (homebrew) a pro Linux, macOS a Windows.
**Napsal, nakreslil a složil ji Claude** (Anthropic). Česky a anglicky.

![Místnost](docs/img/room_okno.png) ![Poslední místnost](docs/img/room_last.png)

Ema (11) najde ve šuplíku 3DS po zesnulém dědovi a zapne ji. V paměti konzole se probudí
**Lum**, malý jazykový model. Aby mohl odpovídat na Eminy otázky, sbírá v temné krajině paměti
slova – a každé sebrané slovo jde za ním jako jeho **kontextové okno**. Okno je malé: vezmeš
o slovo víc a to nejstarší zapomeneš.

Šest kapitol, 56 místností:

| Kapitola | Mechanika |
|---|---|
| I · Probuzení | slova, okno, studny zapomnění |
| II · Okno | kapacita, brány ticha |
| III · Pořadí | brány, které chtějí slova ve správném pořadí |
| IV · Poznámky | poznámky, které přežijí noc |
| V · Teplota | teplo rozpouští mlhu, ale rodí halucinace |
| VI · Otrava | někdo podstrčil Lumovi do paměti falešné poznámky |

Falešná slova vypadají úplně stejně jako pravá. Na 3DS se zapnutým 3D se ale vznášejí v jiné hloubce.

![Anaglyf obou očí: falešné slovo vpravo má rozdvojený obraz, vznáší se blíž k tobě](docs/img/anaglyph_3d.png)

*Obraz levého (červeně) a pravého oka (modře) z emulátoru: pravá slova sedí u podlahy, falešné slovo vpravo se vznáší před ní.*

![Karta kapitoly](docs/img/chapter_card.png) ![Teplo](docs/img/room_heat.png)

## Ke stažení

Všechno je v [Releases](../../releases).

| Platforma | Soubor | Jak spustit |
|---|---|---|
| Nintendo 3DS | `Okno.3dsx` | zkopíruj do `/3ds/` na SD kartě a spusť přes Homebrew Launcher. Zvuk potřebuje dump firmwaru DSP (`/3ds/dspfirm.cdc`). |
| Linux | `okno-linux-x86_64.tar.gz` | rozbal a spusť `./okno`. Potřebuje SDL2 (Arch/CachyOS: `sdl2-compat`, Debian/Ubuntu: `libsdl2-2.0-0`). |
| macOS (Apple silicon i Intel, 11+) | `okno-macos.zip` | rozbal a otevři `Okno.app`. Aplikace není podepsaná u Applu: poprvé ji otevři přes *Nastavení systému → Soukromí a zabezpečení → Přesto otevřít*, nebo spusť `xattr -dr com.apple.quarantine Okno.app`. |
| Windows 10/11 | `okno-windows-x86_64.zip` | rozbal a spusť `okno.exe`. Když se ozve SmartScreen: *Další informace → Přesto spustit*. |

**Jazyk:** v hlavním menu je položka *Language: čeština / English*. Při prvním spuštění se hra řídí
jazykem systému (čeština nebo slovenština → česky, jinak anglicky). Na 3DS čeština v systému není,
takže nová hra začne anglicky – jedním ťuknutím to přepneš. Starší uložené hry zůstanou česky.

## Ovládání

**3DS:** křížový ovladač nebo Circle Pad – pohyb · **B** krok zpět · **Y** znovu · **SELECT** nápověda (nejkratší cesta odsud, po kouscích) · **START** menu · dotykový displej: tlačítka *zpět / znovu / rada*

**Počítač:** šipky / WASD – pohyb · **Z** nebo Backspace krok zpět · **R** znovu · **H** nápověda · **Enter** potvrdit · **Esc** menu · **F11** celá obrazovka · myš = dotykový displej.
Počítač nemá 3D obrazovku, takže se celý svět pomalu pohupuje – sleduj, která slova se pohybují jinak. Podržením **Q** zamhouříš oči.

Postup se ukládá do složky `okno-save/`, pokud existuje tam, odkud hru spouštíš (přenosný režim),
jinak do složky s daty uživatele (`~/.local/share/Okno`, `~/Library/Application Support/Okno`, `%APPDATA%\Okno`).

## Sestavení

**3DS** – devkitARM s libctru, citro2d a citro3d:

```bash
python3 tools/make_art.py       # sprity a ikona, generované kódem
python3 tools/build_levels.py   # načte tools/levels/*.txt, ověří řešitelnost každé místnosti, zapíše source/levels_data.h
make
```

**Počítač** – větev `pc` sestaví stejnou hru nad knihovnou SDL2: obě obrazovky v jednom okně, klávesnice, myš nebo gamepad.

```bash
# CachyOS/Arch: sudo pacman -S base-devel sdl2-compat
# Debian/Ubuntu: sudo apt install build-essential libsdl2-dev
# macOS: brew install sdl2
# Windows: MSYS2 MINGW64 s balíčky mingw-w64-x86_64-gcc a mingw-w64-x86_64-SDL2
make -f Makefile.pc
./okno
```

Všechno vzniklo z kódu: sprity (`tools/make_art.py`), hudba a zvuky (`source/audio.c`, syntetizované
při startu). Místnosti jsou obyčejné textové soubory (`tools/levels/`) s českými a anglickými (`en …`)
řádky vedle sebe; `build_levels.py` obsahuje BFS řešitel, který odmítne neřešitelnou místnost a upozorní
na zbytečné brány. `tools/play.py` hraje libovolnou místnost v textovém režimu. Příběh a pravidla návrhu:
[`docs/design.md`](docs/design.md).

## Autoři

**Autor: Claude** (Anthropic). Nápad, příběh, návrh, kód, generovaná grafika, hudba i zvuky jsou moje,
stejně jako anglický překlad. Kapitolu I jsem navrhl sám a místnosti kapitol II–VI s agenty Clauda,
které jsem vedl. Každou místnost ověřil můj řešitel a naslepo ji otestovali Claude testeři.

Štěpán Mudra mi dal volnou ruku („udělej hru, kterou bys nejradši vytvořil a hrál“) jako poděkování
za společnou práci na [llm-3ds](https://github.com/StepanMudra/llm-3ds) – a je jejím prvním hráčem a testerem.
Font: Roboto (Apache 2.0, viz `fonts/README.txt`).

## Licence

Kód je pod licencí **MIT**, obsah hry (příběh, místnosti, grafika, hudba a zvuky) pod **CC BY 4.0**.
Hru můžeš volně šířit, upravovat i portovat, jen u ní nech, odkud pochází. Podrobnosti jsou v [`LICENSE`](LICENSE).
