# Connecteur Carbonio — Limites connues et points à retravailler

Document vivant : mis à jour à chaque limite identifiée, en cours de route. Ne remplace pas les docs de conception/spec (comptes-rendus d'étapes), c'est le suivi courant des dettes techniques et zones non couvertes.

**Légende de priorité** : 🔴 bloquant pour un usage réel · 🟡 fonctionnel mais fragile/incomplet · 🟢 confort/optimisation

---

## Fonctionnalités non implémentées

- 🔴 **`getMessage` non câblé** — ~~la logique Rust...~~ ✅ **Résolu** — câblé de bout en bout (`nsIStringInputStream`, listener `ICarbonioMessageFetchListener`), validé : le contenu se télécharge correctement (20738 octets confirmés en test réel).
- ~~🔴 **Pas d'ouverture de message**~~ ✅ **Résolu** — cause racine identifiée jusqu'au bout : `dom/ipc/ProcessIsolation.cpp` (`IsolationBehaviorForURI`) et `docshell/base/nsDocShell.cpp` (`CanLoadInParentProcess`) maintiennent chacun une liste blanche de schémas mail codée en dur (`imap`, `mailbox`, `news`, `nntp`, `snews`, `x-moz-ews`, `x-moz-graph`) ; `x-moz-carbonio` en était absent, ce qui poussait Gecko à déclencher un changement de processus à chaque ouverture de message, provoquant la boucle. Correctif : deux patches ajoutant `x-moz-carbonio` à ces listes (`patches/allow-x-moz-carbonio-in-process-isolation.patch` — le correctif décisif — et `patches/allow-x-moz-carbonio-in-parent-process.patch`). **Validé en conditions réelles** : le message s'affiche correctement dans le volet de lecture (simple clic), sans boucle. Détail complet du cheminement : `docs/mecanique-ouverture-message.md`.
  - **Confirmé** : le double-clic (ouverture dans un nouvel onglet) fonctionne également correctement avec ces mêmes patches — validé par Julien.
- 🟡 **Certains messages ne s'affichent toujours pas correctement** (écran blanc, aucune donnée) — cas identifiés après le correctif de la boucle d'ouverture, distinct de celle-ci. Pas encore diagnostiqué : reste à déterminer si c'est systématique pour certains messages précis (contenu/format particulier ?) ou intermittent, et si ça touche le simple clic, le double-clic, ou les deux.
- ~~🔴 Liste de messages non implémentée~~ ✅ **Résolu** — `SearchRequest`/`inid:<id>` + `CarbonioFolder::GetNewMessages` + `CarbonioIncomingServer::SyncAllFolders` : la liste de messages (sujet, expéditeur, date, lu/non-lu, taille) se synchronise et s'affiche correctement, validé en conditions réelles (69 messages, compteur de non-lus exact, hiérarchie imbriquée correcte).
- 🟡 **Pas de sync delta pour les messages** — `SyncMessages()` relance `SearchRequest` en entier à chaque appel, sans token incrémental (contrairement à la sync de dossiers). Potentiellement lourd sur un dossier avec beaucoup de messages.
- 🟡 **Pas de détection des changements sur un message déjà connu** — `UpsertMessageHeader` vérifie seulement la présence locale (par `carbonioMsgId`), jamais si un champ a changé côté serveur (ex : marqué lu/non lu ailleurs). Un message une fois créé localement n'est plus jamais mis à jour.
- 🟡 **Pas de suppression de message détectée** — si un message disparaît côté Carbonio, son en-tête local n'est jamais supprimé.
- 🟢 **Dédoublonnage par balayage linéaire** — `UpsertMessageHeader` parcourt tous les messages déjà présents dans le dossier à chaque message reçu (O(n²) sur l'ensemble d'une synchro). Fonctionnel mais pas optimisé ; à revoir si les dossiers volumineux deviennent lents en pratique.

## Authentification

- 🟡 **Token géré uniquement côté client Carbonio (option B du doc de conception)**, pas intégré au mécanisme partagé `AuthenticationProvider` de `protocol_shared` (option A). Plus rapide à livrer, mais si l'objectif de contribution upstream à Mozilla se précise, il faudra migrer vers l'option A.
- 🟡 **Refresh du token jamais testé en conditions réelles** — on sait que le token dure ~48h (observé), mais le comportement exact à l'expiration (nouveau login automatique vs erreur) n'a jamais été vérifié en pratique, seulement anticipé dans le code.

## Filtrage et hiérarchie de dossiers

- 🟡 **Filtrage mail/non-mail par liste d'IDs codée en dur** (`NON_MAIL_SYSTEM_FOLDER_IDS` dans `sync_folder_hierarchy.rs`) — fonctionne pour le compte de test, mais rien ne garantit que ces IDs système (7, 8, 9, 10, 13, 14, 15, 17) sont universels sur toute instance Carbonio plutôt que spécifiques à celle testée.
- 🟡 **Dossiers partagés ignorés** — les entrées `link` des réponses Carbonio (dossiers partagés par d'autres comptes) ne sont jamais traitées par la synchro de hiérarchie.

## `CarbonioFolder` — comportements non testés

- 🟡 **Une seule méthode overridée volontairement minimale** (`GetDatabase`, `CreateBaseMessageURI`, `GetIncomingServerType`, `GetDBFolderInfoAndDB`, `GetSubFolders`, `GetNewMessages`) — tout le reste (copie, déplacement, compactage, gestion des indésirables, filtres) repose sur le comportement par défaut de `nsMsgDBFolder`, jamais testé avec notre store. Une tentative de l'utilisateur sur une de ces actions pourrait échouer silencieusement ou mal se comporter.
- 🟢 **Pas de `PerformBiff`** — la vérification périodique de nouveaux messages est désactivée par défaut (`GetDefaultDoBiff = false`).

## Compte et création

- 🔴 **Pas d'assistant graphique de création de compte** — création possible uniquement à la main via `prefs.js`/`user.js`. Pas de `CarbonioProtocolHandler` ni d'intégration UI au wizard.

## Hors périmètre phase 1 (rappel, pas des bugs)

- Calendrier, contacts, tâches, chats — jamais synchronisés, exclus par le filtre mail/non-mail.
- Envoi de messages, suppression, déplacement, copie de messages.

## Environnement de développement / process

- 🟡 **Deux patches touchent le cœur `mozilla-central`, pas seulement `comm-central`** — `patches/allow-x-moz-carbonio-in-process-isolation.patch` (`dom/ipc/ProcessIsolation.cpp`) et `patches/allow-x-moz-carbonio-in-parent-process.patch` (`docshell/base/nsDocShell.cpp`). Contrairement aux autres patches du projet, ceux-ci seront écrasés par un `hg pull -u`/mise à jour du checkout mozilla-central et devront être réappliqués manuellement à chaque fois, jusqu'à ce qu'ils soient proposés et acceptés upstream (voir `docs/mecanique-ouverture-message.md`, section "Pour une contribution upstream"). **À suivre** : vérifier qu'ils s'appliquent encore proprement après chaque mise à jour du checkout.
- 🟢 **Sandbox Thunderbird** — `CanCreateUserNamespace() unshare(CLONE_NEWPID): EPERM` nécessite `MOZ_DISABLE_CONTENT_SANDBOX=1` dans l'environnement de test actuel. Gêne d'environnement, pas un bug du connecteur.
- 🟡 **Aucune vérification par compilateur réelle du C++ ni du bridge XPCOM (`lib.rs`)** de mon côté (Claude) — je vérifie l'équilibre des symboles et la cohérence avec le code source réel lu, mais chaque changement C++/bridge nécessite un vrai cycle de compilation pour être confirmé. Le reste du crate Rust (types, client, sous-modules) est vérifiable via un environnement de stubs local, avec un bon niveau de confiance.
- 🟡 **Aucun test automatisé** — contrairement à EWS (`EwsServer.sys.mjs`, `test_folder_sync.js`, `test_syncMessagesForFolder.js`), le connecteur Carbonio n'a pas encore de suite de tests `xpcshell`. Tout est validé manuellement via `./mach run` + profil de test.
