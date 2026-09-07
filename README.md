# g25-userspace

Prototype open source **C++20 / CMake** pour communiquer avec un Logitech G25
sous Windows 11 via le pilote HID Microsoft. Licence GPL-2.0-only.

Le CLI détecte les collections HID candidates, inspecte les capacités
HID/DirectInput et implémente les entrées natives, la plage de rotation et
des essais FFB bornés. Une DLL COM x86/x64 traduit maintenant les **12 effets
standards DirectInput** vers le protocole Logitech. **Les entrées, les butées à
180° et les commandes de force ont été essayées sur un vrai G25 sans les
anciens pilotes Logitech.** Le chargement DirectInput x86/x64 et les rapports
produits ont aussi été vérifiés. Voir [le relevé matériel](docs/validation.md).

Aucun pilote kernel custom, LGS, WinUSB/Zadig ou changement de Secure Boot,
Memory Integrity/HVCI ou signature des pilotes n'est requis par ce prototype.
L'intégration DirectInput ajoute des clés COM/OEM dans le registre de
l'utilisateur courant ; le script d'installation les sauvegarde et les retire.
Les essais actuels ont toutefois été réalisés avec Secure Boot et l'intégrité
de la mémoire désactivés sur le poste ; leur compatibilité reste à valider.

## Compilation Visual Studio 2022

Préparer Visual Studio 2022 avec « Développement Desktop en C++ », le SDK
Windows et CMake ≥ 3.24. Depuis le dossier du projet :

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
ctest --preset release
cmake --preset vs2022-x86
cmake --build --preset release-x86
ctest --preset release-x86
```

L'exécutable est `build/vs2022-x64/Release/g25tool.exe`. Le dossier peut aussi
être ouvert directement dans Visual Studio via CMakePresets.json.

MSVC n'étant pas installé sur le poste de réalisation, la compilation locale
a été effectuée avec Clang/LLVM-MinGW x64. Le workflow Windows MSVC est fourni,
mais n'a pas été exécuté ici. Voir [le relevé de validation](docs/validation.md).

Sur ce poste, les binaires déjà compilés sont dans `build/portable-release`
(x64) et `build/portable-release-x86` (x86) :

```powershell
.\build\portable-release\g25tool.exe list
.\build\portable-release\g25tool.exe info
.\build\portable-release\g25tool.exe monitor --seconds 30
```

## Utilisation

Dans les exemples suivants, `g25tool` désigne l'exécutable compilé ci-dessus.
Brancher la base du volant et, selon la configuration utilisée, le pédalier
et/ou le shifter. Fixer le volant et dégager sa trajectoire. Le mode natif
peut provoquer une réénumération et une calibration
par le firmware. Fermer les jeux et outils qui commandent déjà les moteurs.

```text
g25tool list
g25tool info
g25tool monitor
g25tool monitor --raw --seconds 30
```

`list` montre les index, PID, révision et chemin HID. En présence de plusieurs
volants, utiliser `--device INDEX` d'après une nouvelle liste. `info` est
purement diagnostique : il affiche notamment les longueurs des rapports,
les usages HID et l'indicateur FFB annoncé par DirectInput.

Si un **G25 reconnu** est encore en mode Driving Force/DFP :

```text
g25tool native
g25tool list
g25tool monitor
```

Attendre la réénumération entre `native` et `list`. Aucun changement de mode
n'est envoyé à un simple PID partagé dont la révision réelle est inconnue.
Un descripteur non reconnu provoque un diagnostic, jamais un remplacement
automatique du pilote. Une coupure/reconnexion USB peut rétablir le mode
de démarrage ; les anciens handles/index ne sont pas réutilisés.

`monitor` affiche l'angle estimé, les trois pédales indépendantes, les boutons
1–19, le POV, une interprétation indicative du rapport de boîte et les octets
constructeur. L'affichage des axes est limité à 20 Hz ; les changements de
boutons/POV sont affichés immédiatement. `--raw` affiche tous les rapports.
La correspondance du shifter doit être vérifiée sur le matériel, notamment
en mode séquentiel ; voir [protocole](docs/protocol.md#shifter).

```text
g25tool range 900 --dry-run
g25tool range 900
g25tool range 540
g25tool monitor --range 540
```

`--dry-run` n'ouvre aucun périphérique et montre exactement les buffers Windows.
La plage acceptée est 40–900°. `monitor --range` ne règle pas le volant : il
indique seulement la plage à utiliser pour convertir la mesure en degrés.
Sans cette option, l'hypothèse de 900° est affichée. Aucune lecture de la plage
courante n'a été identifiée. Une sortie « transferred » signifie que Windows
a accepté le transfert, pas que les butées ont été mesurées.

## Essais Force Feedback

Inspecter d'abord les rapports sans matériel :

```text
g25tool test-ffb --dry-run
g25tool center --dry-run
g25tool test-ffb damper --dry-run
```

Puis, pour les essais sur le volant fixé et dégagé :

```text
g25tool test-ffb
g25tool center
g25tool test-ffb damper
g25tool stop
```

La constante vaut environ 30 % de l'échelle de commande positive. Spring
(`center` ou `test-ffb spring`) et damper utilisent la même saturation bornée.
La durée prévue est une seconde, sans réglage de force maximale. Une force
aussi faible peut être difficile à percevoir ; elle n'est pas une mesure
garantie de couple. `center` exerce un ressort temporaire, sans garantir de
recentrer complètement le volant et sans modifier sa calibration.

**Ctrl+C interrompt l'attente et déclenche les commandes d'arrêt.** Les sessions
d'écriture arrêtent aussi les effets à la sortie ou après une exception.
Le test désactive l'autocentre et arrête les quatre slots, y compris des
effets qu'un autre logiciel aurait laissés. Aucun réglage FFB préalable
n'est restauré. Un autre processus g25tool ne peut pas écrire simultanément
dans la même session Windows ; cela ne verrouille pas les logiciels tiers.

Le système ne dispose pas d'un watchdog matériel vérifié. En cas d'arrêt
brutal du processus ou de panne USB/hôte, le stop peut ne pas parvenir au
volant : pouvoir couper l'alimentation pour les premiers essais. Le seul
fait de fermer un handle ne garantit pas l'arrêt du moteur.

Les codes de sortie sont 0 (succès de la commande), 1 (échec matériel/runtime),
2 (arguments invalides), 130 (interruption). `list`/`info` peuvent réussir
avec une liste vide ; ils n'affirment alors aucune présence matérielle.

## Intégration DirectInput des jeux

Construire **les deux architectures** puis enregistrer les DLL pour
l'utilisateur courant :

```powershell
cmake -S . -B build/x64 -A x64
cmake --build build/x64 --config Release
cmake -S . -B build/x86 -A Win32
cmake --build build/x86 --config Release
powershell -ExecutionPolicy Bypass -File scripts/Register-G25FF.ps1 `
  -Action Install `
  -Dll64 build/x64/Release/g25ff.dll `
  -Dll32 build/x86/Release/g25ff.dll `
  -Tray build/x64/Release/g25tray.exe
```

Le script copie les DLL et `g25tray.exe` dans `%LOCALAPPDATA%\g25ff\bin`,
sauvegarde les clés OEM/COM préexistantes puis enregistre les 12 GUID standards
pour le G25 `046d:c299`. Il démarre aussi G25 Control et l'enregistre au login
de l'utilisateur. Il ne demande pas d'élévation et n'installe aucun pilote
noyau. Fermer les jeux avant une mise à jour ou une désinstallation.

### G25 Control dans la zone de notification

Un clic sur l'icône en forme de volant **G25 Control** permet de choisir 180°, 360°, 540° ou
900°. Le choix est mémorisé dans le profil utilisateur et appliqué au volant.
Après un branchement, l'application passe automatiquement un G25 reconnu du
mode de compatibilité au mode natif, attend sa réénumération, puis réapplique
la rotation. Elle réagit aux événements de périphérique Windows et reste au
repos une fois la configuration terminée. Si un jeu tient déjà la sortie HID,
elle affiche que le réglage est en attente et réessaie jusqu'à sa libération.

La DLL FFB n'a pas besoin de `g25tray.exe` pour fonctionner : elle est chargée
dans le processus du jeu. L'application sert au passage en mode natif et à la
rotation après connexion. Le gain maximal reste géré par le jeu.

Seule la base du volant est nécessaire. Le pédalier et le shifter peuvent être
débranchés, y compris lorsqu'un autre pédalier est utilisé. Le descripteur HID
du G25 continue d'annoncer leurs axes et boutons à Windows ; sans accessoire,
ces commandes restent au repos. Un jeu qui accepte plusieurs contrôleurs peut
lier le volant et un pédalier USB séparé indépendamment.

Vérifier la découverte sans jouer d'effet, puis essayer chaque traduction
pendant une seconde :

```text
g25tool info
g25tool directinput-test constant
g25tool directinput-test spring
g25tool directinput-test damper
g25tool directinput-test friction
g25tool directinput-test sine
```

`directinput-test` accepte `constant`, `ramp`, `square`, `sine`, `triangle`,
`saw-up`, `saw-down`, `spring`, `damper`, `inertia`, `friction` et `custom`.
`info` doit afficher `DIDC_FORCEFEEDBACK=yes` et `effects=12 standard=12/12`.
Chaque test est borné à 30 %, respecte Ctrl+C et envoie un arrêt à la fin.
Pour désinstaller et restaurer l'état sauvegardé :

```powershell
powershell -ExecutionPolicy Bypass -File scripts/Register-G25FF.ps1 -Action Uninstall
```

Le pilote additionne et borne Constant, Ramp, les cinq formes périodiques et
Custom sur le slot de force constante. Spring utilise un slot de condition,
Damper et Inertia en partagent un autre, et Friction utilise le quatrième slot
matériel. Pour chaque famille de condition partagée, l'effet actif le plus fort
est retenu. Durées, répétitions, délai de départ, enveloppe, direction, gain
global, pause, reset et arrêt sont pris en charge. La synthèse tourne toutes
les 4 ms et évite les écritures quand l'octet de force quantifié ne change pas.

## Architecture et suite

* [Analyse du protocole, commandes et sources figées](docs/protocol.md)
* [Architecture Windows, DLL DirectInput et backends virtuels](docs/windows_architecture.md)
* [Tests réalisés et procédure de validation matérielle](docs/validation.md)
* [Attributions et réutilisation GPL](THIRD_PARTY_NOTICES.md)

`src/protocol` contient les encodeurs purs ; `src/device` le transport et les
gardes de session ; `src/directinput` la DLL COM ; `src/app` le CLI ; `src/tray`
l'application de notification. Il n'y a pas de backend virtuel factice. La
prochaine étape est un essai dans plusieurs jeux et l'ajustement des conversions
selon les appels réellement observés.

Les tests automatisés n'envoient **aucun rapport au matériel**. Le cœur
portable se compile aussi hors Windows :

```sh
cmake -S . -B build -DG25_BUILD_TOOL=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

Les copies des projets étudiés (`.research`) et outils portables (`.tools`)
sont ignorés par Git et ne sont pas des dépendances du code source.

## Releases et installateur

Les livraisons GitHub contiennent un zip portable et un installateur Windows
par utilisateur. L'installateur installe `g25ff.dll` x64/x86, `g25tray.exe`,
enregistre les cles DirectInput dans `HKCU`, lance G25 Control et ajoute son
demarrage automatique a la session Windows. La desinstallation restaure les
cles sauvegardees par `Register-G25FF.ps1`.

Le pipeline publie aussi des artefacts `dev-<sha>` sur chaque push `main` et
une nightly prerelease `nightly`. Les versions stables sont declenchees par un
tag `v*`, par exemple `v0.1.0`, avec changelog et binaires generes
automatiquement. Voir [release process](docs/release.md).
