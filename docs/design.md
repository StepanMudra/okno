# OKNO – bible návrhu

## Příběh

Šuplík. Stará Nintendo 3DS, která patřila **dědovi**. Děda loni v zimě umřel.
**Ema** (11) ji najde, nabije a zapne. Uvnitř, v paměti konzole, se probudí **Lum** –
malý jazykový model (podobný tomu, který na 3DS opravdu běží v `llm-3ds`). Lum neví, kdo je.
Ema píše otázky na spodní displej; Lum putuje temnou krajinou paměti konzole a sbírá slova,
která potřebuje k odpovědi.

Paměť je plná dědových střípků: uložené hry, pár poznámek, které napsal, jména, místa, vůně
pilin z jeho dílny, jeho pes Bára, jezero, kam Emu brával na ryby, slovo „pomalu“, které pořád
říkal. Lum dědu nikdy *neznal* – nachází jen to, co po něm zbylo.

Tón: tichý, vlídný, trochu melancholický, nikdy sentimentální. Krátké věty. Humor vychází
z Lumovy doslovnosti. Čeština, tykání. Lum mluví v první osobě jen výjimečně; texty místností
jsou většinou vypravěč, který oslovuje hráče „ty“ (= Lum).

| # | Kapitola | Co se stane | Nová mechanika |
|---|---|---|---|
| I | Probuzení | Lum se probudí a odpoví „Ahoj. Jsem tady.“ | pohyb, slova, okno, studny |
| II | Okno | Ema se ptá „Znal jsi dědu?“ Lum toho neunese dost najednou; učí se vybírat, co nést. Končí odpovědí „Nevím. Ale najdu ho.“ | kapacita, brány ticha (`-`), těžší trasy |
| III | Pořadí | Ema chce slyšet o jezeře. Význam závisí na pořadí: „děda chytil rybu“ ≠ „ryba chytila dědu“. Končí první pravdivou větou o dědovi. | brány s pořadím `a>b>c` |
| IV | Poznámky | Ema v noci konzoli vypne: Lum všechno zapomene. Učí se psát poznámky, které přežijí. | kameny poznámek `=`, kapacita poznámek, brány počítají i poznámky, studny poznámky nemažou |
| V | Teplota | Ema chce příběh o dědovi. Aby Lum mohl tvořit, musí se rozpálit – ale teplo přináší halucinace: slova, která vypadají pravdivě, ale jsou vymyšlená. Lum si nesmí vymýšlet vzpomínky na mrtvého. | desky tepla `^` a chladu `_`, mlha `~` průchozí jen v teple, halucinace `~slovo` (falešné, existují jen v teple), brány pravdy `!` |
| VI | Otrava | Někdo zapsal do paměti falešné poznámky („děda nikdy nechytal ryby“, „Bára kousala“). Vypadají přesně jako Lumovy vlastní. Lum musí najít a vymazat, co bylo podstrčeno. Baterie dochází; v poslední místnosti Lum píše poslední pravdivou poznámku pro Emu a pro toho, kdo se probudí po něm. | podstrčená slova `!slovo` v počátečních poznámkách nebo na zemi, brány pravdy, mazání poznámek na `=` s prázdným oknem |

Konec (epilog poslední místnosti VI): Ema: „Díky, Lume. Zítra tě zase zapnu.“ Lum ví, že si to
nebude pamatovat. Ale poznámka ano.

## Pravidla

Jediný zdroj pravdy je docstring v `tools/build_levels.py` – řešitel v něm implementuje přesně
pravidla hry.

## Pravidla návrhu místností

- Mapa nejvýš **16 × 10**, automaticky vycentrovaná. Hrací plochu mají uzavírat zdi `#`.
- Každé písmeno slova je na mapě **jednou** (jedno políčko na písmeno). Stejný *text* se může
  opakovat pod různými písmeny (např. několik `šum` = šumová slova). Šum má text přesně `šum`.
- Brány ukazují požadovaná slova jako barevné tečky; každé jiné slovo v místnosti má vlastní barvu.
- Každá místnost musí být **řešitelná** (jinak ji řešitel odmítne) a **každá brána má být potřeba**
  (řešitel hlásí `gates not needed`; oprav to, pokud to není záměr).
- Dobrá místnost má **jeden jasný nápad**, zjevný naivní pokus, který selže, a *aha*. Raději malé
  mapy s těsnými omezeními než velká bludiště. Žádné dlouhé prázdné chození.
- Ukazatel obtížnosti: `par` (nejkratší řešení) a `states` (velikost prostoru dosažitelných stavů).
  Úvody kapitol: par 10–25, stavy < 200. Finále kapitol: tisíce stavů.
- Nápady na dobré hádanky: vynucené sebrání v chodbě (slovo, kterému se nevyhneš), slova, která
  je nutné sebrat v určitém pořadí, studna, kterou je třeba použít uprostřed cesty, slovo, které
  se vrátí domů do chodby, kterou musíš znovu projít, kapacita přesně rovná počtu slov, která brána
  chce, poznámky jako způsob, jak „přenést“ slovo přes studnu, teplo potřebné k dosažení slova, ale
  chlad k průchodu bránou pravdy, podstrčená poznámka, kterou najdeš vylučovací metodou.
- **Text místnosti**: 1–3 řádky `text:`, dohromady nejvýš ~200 znaků (spodní displej je malý;
  s poznámkami ještě méně – kolem 150). První místnost kapitoly vysvětlí novou mechaniku
  srozumitelně. Další místnosti nesou příběh. Jednou vysvětlená pravidla neopakuj.
- **Epilog**: řádky `after:` se ukážou až po vyřešení místnosti; řádky „Ema: …“ jsou bubliny chatu.
- Slova: krátká česká slova (do 7 písmen se vejdou hezky), spjatá s příběhem (děda, Bára, jezero,
  ryba, pomalu, dílna, piliny, Ema, …).
- Názvy: jedno nebo dvě česká slova.
