# Validation du prototype

## Essais matériels du 7 septembre 2026, après nettoyage

Essais sur le G25 révision `1222`, avec la pile HID Microsoft et sans les
paquets/filtres WingMan retirés ci-dessous. Les résultats de cette section
remplacent les états « non testé » des relevés historiques.

| Vérification | Résultat observé |
| --- | --- |
| Bascule `native` | Transfert réussi, réénumération de `046d:c294` en `046d:c299` |
| Descripteur natif | Rapports Windows **12/8/145 octets**, offsets du décodeur vérifiés avec HidP sur le descripteur réel |
| DirectInput sans LGS, avant `g25ff` | **4 axes, 19 boutons, 1 POV ; FFB non annoncé** |
| DirectInput avec `g25ff`, x64 et x86 | **4 axes, 19 boutons, 1 POV ; FFB annoncé ; 12 effets standard sur 12** |
| Capture d'entrée de 60 secondes | **17 683 rapports** décodés ; mouvement du volant et trois pédales observés |
| Pédales | Accélérateur, frein et embrayage atteignent chacun **0 à 100 %** dans la capture |
| Shifter | Les valeurs indicatives N, 1–6 et R apparaissent dans la capture ; comparaison exhaustive des commandes physiques encore à effectuer |
| Plage 540° | Transfert réussi ; confirmation physique jugée trop difficile par l'utilisateur |
| Plage 180° | Transfert réussi ; **l'utilisateur confirme les butées à environ ±90°** |
| Retour à 900° après essai 180° | Commande transférée ; course totale physique non remesurée |
| Force constante à 3,1 %, une seconde | Transferts de l'effet et des deux commandes d'arrêt réussis ; aucune force ressentie lors de deux essais |
| Ressort à saturation 3,1 %, une seconde | Transferts réussis et arrêt envoyé ; aucune force ressentie |
| Force constante à 12,5 %, répétée | Force ressentie, mais jugée très faible par l'utilisateur |
| Ressort à 30 %, répété | **Effet clairement ressenti et confirmé par l'utilisateur** |
| Amortisseur à 30 %, répété | **Effet clairement ressenti et confirmé par l'utilisateur** |
| Interruption console pendant un effet | `CTRL_BREAK` injecté après 250 ms ; arrêt global et désactivation de l'autocentre transmis ; sortie 130 |
| DirectInput x64, constant/spring/damper | `CreateEffect` et `Start` réussis ; rapports attendus `A6`, spring `EE/4C`, damper `0F/4C`, puis arrêt propre |
| DirectInput x86, constant | Même trajet réussi et même rapport constant `A6`, puis arrêt propre |
| DirectInput x64, neuf effets ajoutés | Ramp, Square, Sine, Triangle, Sawtooth Up/Down, Inertia, Friction et Custom : création, démarrage, mises à jour temporelles le cas échéant et arrêt réussis sur le G25 |
| DirectInput x86, effets ajoutés | Inventaire 12/12 ; Sine, Friction et Custom exécutés sur le G25 avec arrêt propre |
| G25 Control | Processus de notification lancé au login ; menu 180/360/540/900 vérifié par le chemin `WM_COMMAND`, retour final à 900° ; aucune activité CPU mesurable au repos sur deux secondes |

Les builds Release LLVM-MinGW x64 et x86 passent chacun les cinq suites
CTest. Le test COM charge directement la DLL, crée la factory et
`IDirectInputEffectDriver`, vérifie sa version et le déchargement, sans ouvrir
le matériel. La cinquième suite vérifie les formes d'onde, les rampes et
l'indexation des échantillons Custom indépendamment du temps réel. Après
enregistrement, un inventaire DirectInput n'émet aucun
rapport HID ; l'ouverture en écriture reste différée jusqu'au premier ordre FFB.

Les valeurs d'angle du moniteur dépendent de la plage fournie en argument :
elles ne mesurent pas la course réelle. La validation à 180° repose sur le
retour humain, et pas simplement sur des valeurs affichées de −90 à +90°.
Les 19 boutons, toutes les directions du POV et le mode séquentiel ne sont
pas encore validés individuellement. De petites variations des pédales près
du repos apparaissent dans les captures ; aucune zone morte n'est appliquée.

Le relevé en lecture seule indique `VirtualizationBasedSecurityStatus=2`,
mais `SecurityServicesRunning=[0]` et `HypervisorEnforcedCodeIntegrity\\Enabled=0`.
Windows indique aussi `UEFISecureBootEnabled=0`. **Ces essais ne valident donc
pas le fonctionnement avec intégrité de la mémoire et Secure Boot actifs.**
Aucun de ces paramètres n'a été modifié pour les essais ou le nettoyage.

Les captures et diagnostics locaux sont conservés sous
`build/hardware-tests-20260907/`, ignoré par Git. L'interruption console a été
validée par les transferts et le code de sortie ; la sensation physique d'un
effet interrompu, la fermeture forcée et la déconnexion restent à tester.
La couche DirectInput complète est implémentée et enregistrée par utilisateur.
Son utilisation dans un vrai jeu a été confirmée avec les trois effets initiaux.
La sensation physique propre à chacun des neuf effets ajoutés reste à comparer,
même si leur chaîne DirectInput et leurs transferts HID ont été validés.

## Nettoyage effectué le 7 septembre 2026

À la demande de l'utilisateur, les paquets WingMan 5.09.129.0 `oem25.inf`
(WmJoyHid), `oem26.inf` (WmVirHid) et `oem27.inf` (WmBEnum) ont été exportés
puis désinstallés avec PnPUtil. Leur suppression a réussi, sans `/force`
ni redémarrage demandé. Les périphériques virtuels associés, les anciennes
instances du volant et les enregistrements OEM/FFB de ses modes C294/C298/C299
ont été retirés après sauvegarde. Les autres périphériques Logitech ne font
pas partie des cibles. Aucune protection Windows n'a été modifiée.

Relevé PnP après nettoyage : les nœuds USB et HID du G25 utilisent tous deux
`input.inf`, fournisseur **Microsoft**, statut OK, sans filtre WingMan.
La pile USB comprend `HidUsb` ; la pile joystick comprend `hidgamepad` et
`HidUsb`. Les services WmHidLo/WmFilter/WmBEnum/WmXlCore/WmVirHid ne sont
plus retournés par l'inventaire des pilotes système.

Le volant s'annonce maintenant **046d:c294, révision 1222**, en mode de
compatibilité. `g25tool info` détecte correctement le G25 et relève :

* rapports Windows input/output/feature : **8/8/0 octets** ;
* volant X sur 10 bits, axe Y combiné, 12 boutons et un POV ;
* sortie constructeur `FF00:03`, sept octets avec identifiant nul ;
* DirectInput : **2 axes, 12 boutons, 1 POV, FFB non annoncé**.

C'est désormais une détection constatée avec la pile Microsoft. Cela ne
valide pas encore les entrées complètes en mode natif, les butées ou les
effets. Aucune commande `native`, `range` ou FFB du prototype n'a été envoyée
pendant le nettoyage ; la prochaine étape est le passage natif contrôlé,
suivi d'un nouveau relevé de descripteur et des tests d'entrée.

Sauvegarde locale :
`build/driver-cleanup-20260907/backup-20260907-085114/` (32 fichiers).
Le dossier parent conserve `cleanup.ps1`, le journal `cleanup.log`, le
résultat `result.json`, les inventaires et les sorties du CLI avant/après.
Il est ignoré par Git. Le script est propre à ce PC et vérifie les empreintes
des trois INF avant toute mutation ; ne pas le réutiliser sur un autre poste
ni après réattribution des numéros de paquets.

## Mise à jour du 7 septembre 2026 : G25 présent, ancien pilote actif

Le G25 connecté est détecté comme `046d:c299`, révision `1222`. Cette
détection **ne valide pas le fonctionnement sans le pilote Logitech** :

* Les deux nœuds USB/HID utilisent le paquet Logitech `oem25.inf`,
  version `5.9.129.0`.
* `pnputil /enum-devices /instanceid ... /stack` confirme `WmHidLo` dans
  la pile USB et `WmFilter` dans la pile HID, aux côtés de `HidUsb`.
  Les services `WmHidLo` et `WmFilter` sont démarrés.
* DirectInput annonce cinq axes, 19 boutons, un POV et
  `DIDC_FORCEFEEDBACK=yes`. La clé OEMForceFeedback du G25 est encore
  enregistrée avec le CLSID Logitech `{8D533A4D-7A5F-11D3-8297-0050DA1A72D3}`.
  Cet indicateur FFB ne démontre donc pas une prise en charge native Microsoft
  ni une fonctionnalité apportée par notre prototype.
* Les capacités HID actuelles annoncent **13/8/145 octets** pour les rapports
  input/output/feature, identifiant compris, avec un axe Slider supplémentaire.
  Le décodeur initial attend 12 octets en entrée et refuse ce descripteur.
  On ne peut pas attribuer avec certitude cette différence au filtre Logitech
  avant une comparaison avec la pile Microsoft seule.

Seules des requêtes de diagnostic ont été exécutées : aucun changement de
pilote, de registre ou de sécurité, et aucune commande moteur. La compilation
et les résultats automatisés historiques ci-dessous restent distincts de
ce relevé matériel.

Pour valider l'objectif « sans LGS », comparer sur un Windows où ce paquet
et ses filtres ne sont pas actifs, puis refaire `list`/`info` après une
reconnexion physique. La suppression des anciens composants sur ce PC est
une opération système distincte, non effectuée pendant cet audit. Fermer le
Profiler seul ne suffit pas puisque les pilotes sont chargés dans la pile.
Une éventuelle désinstallation doit aussi considérer les enregistrements
FFB résiduels et les autres périphériques utilisant le même paquet.

Référence de l'outil de diagnostic :
[PnPUtil, option /stack](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax).

## Relevé initial du 6 septembre 2026

Relevé du 6 septembre 2026. La présence d'un exécutable fonctionnel ne valide
pas le comportement du matériel.

## Vérifications réellement effectuées

| Vérification | Résultat |
| --- | --- |
| Système local | Windows 11 Professionnel, build 26200, x64 |
| Compilateur local | LLVM-MinGW 20260826, Clang 23.1.0, UCRT x64 |
| Générateur | CMake 4.4.3, MinGW Makefiles |
| Compilation Debug du cœur puis du CLI | Réussie, aucun avertissement avec `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion` |
| Compilation Release du CLI | Réussie, aucun avertissement avec les mêmes options |
| Tests du protocole et de la garde d'arrêt | 69 vérifications réussies |
| Tests Windows des capacités, sélection, exclusion des écrivains et annulation | 23 vérifications réussies |
| Scénarios CLI dry-run et arguments invalides | 18 scénarios réussis |
| CTest Debug et Release | 3 suites sur 3 réussies dans chaque configuration |
| `g25tool list` sur le PC | Aucune collection de volant Logitech correspondante |
| `g25tool info` sur le PC | Aucun volant correspondant dans HID ou DirectInput |
| Dépendances du binaire portable | DLL Windows/HID/SetupAPI/DirectInput et UCRT ; pas de DLL HIDAPI/libusb/LLVM à déployer |
| Compilation MSVC / workflow GitHub | Configuration fournie ; non exécutée sur ce poste sans Visual Studio |
| Entrées d'un G25 réel | **Non testées : volant absent** |
| Bascule USB, butées, effet physique, arrêt moteur | **Non testés : volant absent** |
| FFB dans les jeux | **Non implémenté à ce jalon** |

Les tests n'énumèrent ni n'ouvrent les périphériques HID. Les fixtures
d'entrée sont des vecteurs synthétiques construits à partir du descripteur
publié, pas des captures présentées comme provenant d'un G25 réel.

Le test d'interruption Windows déclenche l'événement d'arrêt pendant une
attente d'une seconde et vérifie son réveil anticipé. Il ne simule pas une
fermeture forcée du système et ne mesure pas le délai USB d'arrêt du moteur.
Le chemin du gestionnaire Ctrl+C et la fermeture de fenêtre restent à essayer
sur le poste avec le volant, après le test normal faible.

Les paramètres de sécurité Windows n'ont pas été modifiés. Leur état n'a pas
été certifié par ces tests ; l'absence de pilote custom ne remplace pas le
relevé HVCI/Secure Boot du futur essai matériel.

## Reproduire la compilation portable sur ce poste

Les outils ont été obtenus depuis les releases officielles
[LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260826) et
[CMake](https://github.com/Kitware/CMake/releases/tag/v4.4.3), extraits dans
`.tools` sans installation système. La signature Authenticode de cmake.exe
a été vérifiée valide. Aucune modification du PATH global n'a été faite.

```powershell
$cmake = Join-Path $PWD '.tools/cmake-4.4.3-windows-x86_64/bin/cmake.exe'
$ctest = Join-Path $PWD '.tools/cmake-4.4.3-windows-x86_64/bin/ctest.exe'
$compiler = Join-Path $PWD '.tools/llvm-mingw-20260826-ucrt-x86_64/bin/clang++.exe'
$make = Join-Path $PWD '.tools/llvm-mingw-20260826-ucrt-x86_64/bin/mingw32-make.exe'
& $cmake -S . -B build/portable-release -G 'MinGW Makefiles' "-DCMAKE_CXX_COMPILER=$compiler" "-DCMAKE_MAKE_PROGRAM=$make" -DCMAKE_BUILD_TYPE=Release
& $cmake --build build/portable-release --parallel 4
& $ctest --test-dir build/portable-release --output-on-failure
```

Pour MSVC, utiliser les commandes du README. Les deux configurations restent
distinctes pour éviter de réutiliser un cache CMake d'un autre compilateur.

## Procédure sur un vrai G25 sans Logitech Gaming Software

Conserver les sorties ci-dessous avec la version/build Windows, révision du
volant, origine du pilote actif et état de Secure Boot/Memory Integrity.
Ne pas modifier ces protections pour essayer de faire passer un test.

1. **Avant toute écriture** : fixer le volant, connecter pédales et shifter,
   dégager sa rotation, fermer les jeux et contrôleurs FFB concurrents.
   Exécuter `list`, puis `info`. Relever VID/PID, révision et longueurs des
   rapports. Vérifier le pilote Microsoft actif dans le Gestionnaire de
   périphériques et conserver le diagnostic DirectInput. S'il reste un ancien
   filtre Logitech, ce poste ne constitue pas encore une validation « sans LGS ».
2. **Mode** : si un G25 reconnu est en `c294/c298`, inspecter `native --dry-run`,
   puis utiliser `native` ; attendre et relancer `list`/`info`. Attendu : PID
   `c299` et nouveaux handles valides. Si la commande signale une déconnexion,
   vérifier la liste avant de réessayer : la bascule peut avoir eu lieu.
   Un descripteur inconnu impose d'analyser le relevé, pas d'outrepasser le filtre.
3. **Jalon 1, entrées** : `monitor --raw --seconds 30`, puis `monitor`.
   Tourner doucement, actionner chaque pédale séparément sur toute sa course,
   chaque bouton et le POV. Essayer N, 1–6, R et le mode séquentiel. Comparer
   les boutons réellement actifs au champ `Gear*` indicatif et aux octets
   constructeur. Tester Ctrl+C et déconnexion pendant une lecture. Conserver
   les rapports bruts pour ajouter des fixtures matérielles après examen.
4. **Jalon 2, plage** : inspecter `range 540 --dry-run`, envoyer `range 540`,
   puis `monitor --range 540`. Mesurer la rotation effective et les butées,
   sans confondre la conversion d'affichage avec une mesure physique. Répéter
   avec 900. Vérifier aussi le comportement après débranchement/rebranchement.
5. **Jalon 3, constante** : inspecter `test-ffb --dry-run`, puis `test-ffb`.
   Attendu : très faible force pendant environ une seconde et retour au repos.
   Garder accès à la coupure d'alimentation. Répéter en interrompant tôt avec
   Ctrl+C, puis tester la fermeture de console. `stop` doit fonctionner
   indépendamment après une session terminée. Ne pas lancer deux contrôleurs
   de moteurs en parallèle.
6. **Conditions** : `center` (spring faible temporaire) puis `test-ffb damper`.
   Le damper se ressent lors d'un mouvement, pas nécessairement au repos.
   Vérifier l'arrêt, sans augmenter immédiatement l'intensité si l'effet est
   imperceptible. Le seuil de frottement mécanique peut masquer l'essai faible.

Un jalon est validé seulement après consignation de son résultat physique.
Si WriteFile échoue : relever le code Win32, PID, capacités et pilote actif.
Le projet ne propose pas de désactiver HVCI, de remplacer HID par Zadig ou
d'installer un pilote non signé pour contourner l'échec.

## Limites à conserver visibles

* Le descripteur de référence vient du mode G25 d'un G29 dans lg4ff_userspace.
  Ses offsets ont maintenant été vérifiés sur un G25 réel de révision `1222`
  avec la pile Microsoft ; cela ne couvre pas toutes les révisions.
* La révision G25 reconnue exclut les G27 à signature `123x` et les G29 connus.
  Un firmware inconnu est un cas d'analyse ; aucune option « force » ne permet
  de lui envoyer aveuglément les commandes.
* Le mapping Profiler du shifter peut différer du HID brut ; les champs bruts
  restent visibles pour éviter de masquer cette incertitude.
* Les timeouts et Ctrl+C sont logiciels. Aucun watchdog du volant n'est
  démontré, et annuler une I/O ne garantit pas que le matériel a reçu un stop.
* Le prototype garde la plage demandée mais laisse les effets arrêtés et
  l'autocentre désactivé ; il ne peut pas restaurer des réglages non lus.
* Aucun backend virtuel, pilote kernel ni enregistrement OEM DirectInput n'a
  été installé. L'intégration aux jeux est une étape distincte documentée
  dans windows_architecture.md.
