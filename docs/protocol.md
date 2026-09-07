# Protocole Logitech G25 — analyse avant implémentation

Analyse commencée le 6 septembre 2026 et complétée par les essais matériels du
7 septembre. Ce document distingue les faits présents dans les sources des
résultats observés, consignés dans `validation.md`.

## Sources et réutilisation

Les révisions étudiées sont figées pour rendre les commandes auditables :

* **new-lg4ff**, `2092db19f7b40854e0427a1b2e39eda9f8d0c3cd` :
  [hid-lg4ff.c][kernel], [hid-lg.c][hid], [hid-ids.h][ids]. C'est un module
  Linux HID, dérivé de hid-logitech : identification, changements de mode,
  réglages sysfs, traduction des effets Linux, quatre slots matériels,
  ordonnanceur hrtimer et mélange des effets. Les types périodiques sont
  calculés sur l'hôte et alimentent la force constante. Le fichier protocole
  porte `SPDX-License-Identifier: GPL-2.0-or-later`, avec les copyrights
  de Simon Wood (2010) et Bernat Arlandis (2019).
* **lg4ff_userspace**, `d81ccb5d23716fa859b5f72d7e26d4ac8e6ba67d` :
  [configure.c][configure], [switch_mode.c][switch], [force_feedback.c][ff],
  [driver_loops.c][loops], [rd_g25][descriptor]. Port C incomplet utilisant
  HIDAPI (hidraw ou libusb), pthreads, événements evdev et `/dev/uinput`.
  Son fichier LICENSE contient la GPL v2. Son README précise que le mode G25
  n'a été essayé que sur un G29 émulant le G25 : ce n'est pas une validation
  de toutes les révisions de G25 réelles.

Le prototype reprend en C++ les encodages et le découpage des entrées, avec
attribution dans le code et dans THIRD_PARTY_NOTICES.md, sous GPL-2.0-only.
Les parties Linux (uinput, ioctl evdev, sysfs, hrtimer) ne sont pas compilables
sous Windows. Elles ne sont pas embarquées. Le moteur complet de new-lg4ff
reste la référence de la traduction DirectInput, notamment pour la synthèse
temporelle et les effets conditionnels.

Les sources Linux amont correspondantes sont
[drivers/hid/hid-lg4ff.c](https://github.com/torvalds/linux/blob/v6.12/drivers/hid/hid-lg4ff.c),
[hid-lg.c](https://github.com/torvalds/linux/blob/v6.12/drivers/hid/hid-lg.c) et
[hid-ids.h](https://github.com/torvalds/linux/blob/v6.12/drivers/hid/hid-ids.h).
Les références précises ci-dessous pointent vers le fork effectivement lu.

## Identité USB et modes

VID Logitech : `046d`. Attention : PID signifie ici Product ID USB,
pas la classe HID Physical Interface Device utilisée pour le FFB standard.

| PID | Mode annoncé | Traitement du prototype |
| --- | --- | --- |
| `c294` | Driving Force / Formula EX, mode de compatibilité possible | Listé ; bascule uniquement si révision reconnue G25 |
| `c298` | Driving Force Pro, autre mode possible du G25 | Même précaution |
| `c299` | G25 natif, également émulable par G27/G29 | Entrées natives ; écritures limitées aux G25 reconnus |
| `c29b` | G27 natif | Diagnostic uniquement, aucun envoi G25 |

Sources : [constantes USB][ids], `lg4ff_multimode_wheels`,
`lg4ff_main_checklist`, `lg4ff_identify_multimode_wheel` dans [hid-lg4ff.c][kernel].
Le G25 est identifié par `(bcdDevice & 0xff00) == 0x1200`, **après** exclusion
du G27 `(bcdDevice & 0xfff0) == 0x1230`. Le G29 a notamment les signatures
`(rev & 0xfff8) == 0x1350` et `(rev & 0xff00) == 0x8900`.
Un PID de compatibilité seul ne prouve donc jamais la présence d'un G25.
Windows expose cette révision dans `HIDD_ATTRIBUTES.VersionNumber`.

Le passage en G25 natif envoie `F8 10 00 00 00 00 00`
(`lg4ff_mode_switch_ext16_g25`). Le passage en DFP utilise `F8 01 ...`.
Le G25 ne supporte pas les retours arbitraires entre modes : le code Linux
refuse notamment de revenir vers un PID inférieur après passage natif.
Un débranchement/rebranchement peut être nécessaire pour revenir au mode
de démarrage. La famille de commandes `F8 09 ...` concerne d'autres modèles
(G27/DFGT/G29) et n'est pas envoyée au G25 réel.

Une bascule peut invalider le handle et changer PID/descripteur. `native`
envoie la commande puis ferme le handle ; relancer `list` et `monitor`
après réénumération. Le CLI ne choisit jamais arbitrairement un autre volant
après une déconnexion. `list`, `info` et `monitor` ne basculent pas le mode
silencieusement ; un mode incompatible produit une instruction explicite.

## Rapports HID natifs

Le descripteur [rd_g25][descriptor] annonce une collection Joystick
(page `01`, usage `04`), sans Report ID explicite :

| Type | Charge utile USB | Buffer HID Windows |
| --- | --- | --- |
| Input | 11 octets, 88 bits | 12 octets, premier octet `00` |
| Output | 7 octets propriétaires | 8 octets, premier octet `00` |
| Feature | 144 octets propriétaires | 145 octets, premier octet `00` |

Ces tailles sont vérifiées avec `HidP_GetCaps` avant le décodage/envoi.
Les usages et domaines logiques sont également contrôlés. Un descripteur
différent doit être étudié, et non interprété avec des offsets approximatifs.
Le parseur Windows `HidP_GetUsageValue/GetUsages` est exercé sur des buffers
locaux à bit unique pour vérifier que les offsets correspondent au décodeur.
Ces buffers de vérification ne sont jamais envoyés au volant.
Les Feature reports ne sont ni lus ni écrits : leur sémantique n'est pas
établie par les sources étudiées.

Les modes de compatibilité ont d'autres usages Output : `01:03` pour Driving
Force, `01:02` pour DFP dans `df_rdesc_fixed/dfp_rdesc_fixed` de [hid-lg.c][hid].
Le filtre de sortie traite ces modes séparément, toujours avec exactement sept
octets et Report ID 0. Une variante DF-EX publiée par lg4ff_userspace annonce
plus de sorties (capture d'un G29) : elle n'est pas acceptée pour une bascule
G25. Les corrections Linux concernent aussi les pédales séparées cachées dans
les usages constructeur du descripteur original ; elles ne sont pas présentes
automatiquement dans le pilote HID Windows. Cela renforce l'intérêt du mode natif.

Offsets ci-dessous dans la **charge utile**, sans le `00` de Windows ; bits
numérotés depuis le bit faible du premier octet :

| Bits | Champ | Domaine / usage |
| --- | --- | --- |
| 0–3 | POV | 0–7 directions, 8 repos ; autres valeurs réservées |
| 4–22 | Boutons | 19 bits, page `09`, usages 1–19 |
| 23–25 | Constructeur | Conservés comme données brutes |
| 26–39 | Volant X | 14 bits, 0–16383 ; `(p[3] >> 2) \| (p[4] << 6)` |
| 40–47 | Accélérateur Z | `p[5]`, 255 relâché, 0 enfoncé |
| 48–55 | Frein Rz | `p[6]`, même convention |
| 56–63 | Embrayage Y | `p[7]`, même convention |
| 64–87 | Constructeur | `p[8..10]`, conservés bruts |

Découpage repris de `uinput_g25_g27_emit` dans [driver_loops.c][loops].
Les pédales sont distinctes dans le rapport natif ; leur combinaison dans
Linux est une transformation logicielle. `monitor` indique un angle estimé
à partir de la plage donnée par `--range` (900 par défaut, hypothèse affichée).
Le protocole étudié ne fournit pas de lecture de la plage courante. Après
`range 540`, utiliser `monitor --range 540`. Ce réglage d'affichage ne change
pas le matériel et ne constitue pas une calibration physique.

### Shifter

[Logitech documente][gears] les rapports 1–6 et R comme boutons DirectX
8–14 (numérotation à partir de zéro), soit boutons affichés 9–15.
Le CLI expose cette interprétation **indicative**, plus tous les boutons,
le POV et les octets constructeur. Plusieurs bits de rapport actifs donnent
`?`, aucun donne `N`. Cette correspondance issue du Profiler XP/Vista doit
être confrontée au HID brut sans LGS ; Linux ne nomme pas les rapports de
boîte, il transmet les boutons. Aucun seuil analogique de shifter n'est
inventé. Le mode séquentiel reste observable par les boutons, sans déduire
un rapport de boîte absolu. Ne pas confondre les mappings G25 et G27/G29.

## Commandes émises

Toutes les lignes de cette table représentent les **sept octets Logitech**.
Le transport Windows ajoute `00` devant, envoie avec `WriteFile` et vérifie
le nombre d'octets transférés. Aucun repli automatique vers WinUSB,
`SetFeature` ou une commande différente.

| Fonction | Charge utile | Source exacte dans [hid-lg4ff.c][kernel] |
| --- | --- | --- |
| Mode G25 | `F8 10 00 00 00 00 00` | `lg4ff_mode_switch_ext16_g25`, `lg4ff_get_mode_switch_command` |
| Plage | `F8 81 LL HH 00 00 00` | `lg4ff_set_range_g25` |
| 900° | `F8 81 84 03 00 00 00` | même fonction ; 900 = `0384` |
| 540° | `F8 81 1C 02 00 00 00` | même fonction ; 540 = `021C` |
| Stop quatre slots | `F3 00 00 00 00 00 00` | `lg4ff_stop_effects` |
| Désactiver autocentre | `F5 00 00 00 00 00 00` | `lg4ff_set_autocenter_default`, magnitude 0 |
| Constante slot 0 | `11 00 XX 00 00 00 00` | `lg4ff_update_slot`, branche `FF_CONSTANT` |
| Spring slot 1 | `21 0B D1 D2 KK SS CL` | même fonction, branche `FF_SPRING` |
| Damper slot 2 | `41 0C K1 S1 K2 S2 CL` | même fonction, branche `FF_DAMPER` |
| Friction slot 3 | `81 0E K1 S1 K2 S2 CL` | même fonction, branche `FF_FRICTION` |
| Arrêt slots 0/1/2/3 | `13` / `23` / `43` / `83`, puis zéros | opération stop de `lg4ff_update_slot` |

La plage acceptée est 40–900° inclus (`lg4ff_devices`). Le prototype refuse
les valeurs hors domaine au lieu de les corriger silencieusement. Une écriture
réussie confirme le transfert Windows, pas un accusé de réception firmware
ni la mesure des nouvelles butées.

### Force Feedback

L'octet 0 associe le masque des slots dans le demi-octet haut et l'opération
dans le bas : `1` télécharge/joue, `C` met à jour, `3` arrête. Les quatre
slots ont les masques `10`, `20`, `40`, `80`. Le moteur new-lg4ff gère plus
d'effets logiciels que de slots matériels ; il mélange les constantes et
les formes périodiques et affecte les conditions aux slots disponibles.

Constante : `XX = (force_signée_16_bits + 32768) >> 8`.
`80` est neutre, `00` et `FF` sont proches des extrêmes, **pas des arrêts**.
Le test fixe la force à +9830, donc `A6`, environ 30 % de l'échelle positive
de commande, pendant une seconde au maximum prévue. Cette proportion n'est
pas une mesure de couple. Aucune option CLI ne permet une force maximale.

Spring : positions de début/fin de zone morte converties de signé 16 bits
vers 11 bits ; les huit bits hauts vont en `D1/D2`, les trois bas et les
signes des coefficients en `SS`. Après traitement du seuil 2048 du firmware,
les coefficients sont quantifiés sur quatre bits dans `KK`, et la saturation
sur huit bits dans `CL`. L'encodage du prototype est une adaptation attribuée
de cette branche. `center` utilise un spring symétrique à saturation de 30 %,
pour une seconde ; ce n'est ni une calibration ni un maintien permanent.

Damper : coefficients signés de chaque côté, modules quantifiés sur quatre
bits (`min(abs(k)*2,65535)>>12`), signes séparés, saturation sur huit bits.
Inertia utilise ce même encodage et partage le slot damper ; le plus fort des
effets actifs est retenu. Friction reprend les signes et la saturation, avec
des coefficients sur huit bits (`min(abs(k)*2,65535)>>8`) dans son propre slot.

Ramp, Square, Sine, Triangle, Sawtooth Up, Sawtooth Down et Custom sont
synthétisés sur l'hôte toutes les 4 ms. Leur niveau instantané, leurs gains,
directions, durées, répétitions, délais et enveloppes sont convertis, mélangés
avec les constantes actives, puis bornés et envoyés dans le slot constant.
La première commande utilise l'opération `1`; les variations suivantes
utilisent `C`. Custom accepte un canal et copie les échantillons fournis par
DirectInput pour les rejouer selon leur période d'échantillonnage.

Autocentre constructeur : `FE 0D A A B 00 00`, suivi de `14 00 ...`, avec
calcul non linéaire de A/B dans `lg4ff_set_autocenter_default`. Documenté mais
non activé dans le prototype : le spring borné évite de laisser un autocentre
actif après la fermeture. `0D <mode>` règle la boucle firmware dans
`lg4ff_init_slots` ; non nécessaire au test minimal, non envoyé.

Les douze GUID d'effets standard sont enregistrés et annoncés à DirectInput.
Le CLI `directinput-test` propose un essai borné d'une seconde pour chacun.

### Initialisation et arrêt

1. Énumérer sans changer les pilotes, vérifier VID/PID/révision/descripteur.
2. Si nécessaire, `native`, puis nouvelle énumération après changement de PID.
3. Lire les entrées sans initialisation FFB pour le premier jalon.
4. Pour une session d'écriture : préparer la garde d'arrêt avant le premier
   transfert, envoyer stop + autocentre désactivé ; envoyer la commande voulue.
5. Pour un effet : durée maximale du CLI d'une seconde, attente interruptible
   par événement Windows, arrêt explicite puis arrêt de secours RAII.
6. À la sortie/erreur d'une session d'écriture, tenter stop et désactivation
   d'autocentre indépendamment, même si l'une des deux écritures échoue.

Ctrl+C réveille immédiatement l'attente. Les écritures overlapped ont un délai
de garde ; une écriture annulée est terminée avant de libérer son buffer.
La latence d'arrêt réelle dépend de Windows et de l'USB. Les sources ne prouvent
pas de watchdog matériel : arrêt brutal du processus, panne hôte ou câble
débranché peuvent empêcher le stop. Une durée logicielle n'est pas une garantie
matérielle. Garder le volant dégagé et pouvoir couper son alimentation pour
les premiers essais. Aucun essai moteur n'est exécuté par CTest.

[kernel]: https://github.com/berarma/new-lg4ff/blob/2092db19f7b40854e0427a1b2e39eda9f8d0c3cd/hid-lg4ff.c
[hid]: https://github.com/berarma/new-lg4ff/blob/2092db19f7b40854e0427a1b2e39eda9f8d0c3cd/hid-lg.c
[ids]: https://github.com/berarma/new-lg4ff/blob/2092db19f7b40854e0427a1b2e39eda9f8d0c3cd/hid-ids.h
[configure]: https://github.com/Kethen/lg4ff_userspace/blob/d81ccb5d23716fa859b5f72d7e26d4ac8e6ba67d/configure.c
[switch]: https://github.com/Kethen/lg4ff_userspace/blob/d81ccb5d23716fa859b5f72d7e26d4ac8e6ba67d/switch_mode.c
[ff]: https://github.com/Kethen/lg4ff_userspace/blob/d81ccb5d23716fa859b5f72d7e26d4ac8e6ba67d/force_feedback.c
[loops]: https://github.com/Kethen/lg4ff_userspace/blob/d81ccb5d23716fa859b5f72d7e26d4ac8e6ba67d/driver_loops.c
[descriptor]: https://github.com/Kethen/lg4ff_userspace/blob/d81ccb5d23716fa859b5f72d7e26d4ac8e6ba67d/rd_g25
[gears]: https://support.logi.com/hc/en-us/articles/360023207554-Programming-the-G25-Shifter-positions-with-Logitech-Profiler
