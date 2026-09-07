# Provenance et licence

Ce projet est distribué sous GNU GPL version 2 uniquement (`GPL-2.0-only`).
Le texte intégral se trouve dans LICENSE. Conserver les mentions ci-dessous
et fournir le code source correspondant lors de la distribution de binaires
selon les conditions de cette licence.

Les encodeurs dans `src/protocol/logitech_protocol.cpp` et
`src/protocol/force_feedback.cpp` adaptent les commandes et calculs de :

* new-lg4ff, `hid-lg4ff.c`, révision
  `2092db19f7b40854e0427a1b2e39eda9f8d0c3cd`,
  https://github.com/berarma/new-lg4ff
  Copyright (c) 2010 Simon Wood <simon@mungewell.org>
  Copyright (c) 2019 Bernat Arlandis <berarma@hotmail.com>
  SPDX amont : GPL-2.0-or-later. Cette adaptation choisit GPL v2.
* lg4ff_userspace, révision
  `d81ccb5d23716fa859b5f72d7e26d4ac8e6ba67d`,
  https://github.com/Kethen/lg4ff_userspace
  Projet de Kethen, LICENSE GNU GPL v2 ; `configure.c`, `switch_mode.c`,
  `driver_loops.c` et `rd_g25` servent de référence croisée et de source
  pour le découpage du rapport natif. `tests/fixtures/rd_g25.txt` conserve
  le descripteur publié, issu d'une émulation G25 sur G29 selon le README.
* xinput-ffb-driver, révision
  `f79f0fa91ce5f56ea95ce2b64e6cdd9ace6f6429`,
  https://github.com/mentalfoundry/xinput-ffb-driver
  Copyright (c) 2026 mentalfoundry. Son implémentation
  `IDirectInputEffectDriver` et son schéma OEM ont servi de référence croisée
  pour la couche COM Windows. Licence amont MIT :

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

Modifications : API C++20 pures, limites explicites, trame Windows avec
identifiant nul, tests de vecteurs, transport Win32, garde d'arrêt et traduction
DirectInput vers les quatre slots matériels du G25, avec synthèse temporelle
des forces périodiques.
Le code uinput, evdev et le moteur temps réel complet ne sont pas embarqués.

Les outils de développement locaux (`.tools`) et copies de recherche
(`.research`) ne font pas partie du projet distribué et sont ignorés par Git.
Les liens et fonctions sources sont détaillés dans docs/protocol.md.
