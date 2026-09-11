# Connecteur Carbonio — Limites connues et points à retravailler

Document vivant : mis à jour à chaque limite identifiée, en cours de route. Ne remplace pas les docs de conception/spec (comptes-rendus d'étapes), c'est le suivi courant des dettes techniques et zones non couvertes.

**Légende de priorité** : 🔴 bloquant pour un usage réel · 🟡 fonctionnel mais fragile/incomplet · 🟢 confort/optimisation

---

## Fonctionnalités non implémentées

- 🔴 **`getMessage` non câblé** — la logique Rust (`CarbonioClient::get_message`) est écrite et validée empiriquement, mais le bridge XPCOM (`lib.rs`) la stub encore (`NS_ERROR_NOT_IMPLEMENTED`). Il manque le passage du MIME brut vers un `nsIInputStream` côté C++ (piste identifiée : `nsIStringInputStream`, voir l'article de Brendan Abolivier).
- 🔴 **Pas d'ouverture de message** — aucun protocole/service (`x-moz-carbonio`, équivalent d'`ExchangeService`/`ExchangeProtocolHandler`) pour afficher le contenu d'un message double-cliqué. Dépend du point précédent.
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

- 🟢 **Sandbox Thunderbird** — `CanCreateUserNamespace() unshare(CLONE_NEWPID): EPERM` nécessite `MOZ_DISABLE_CONTENT_SANDBOX=1` dans l'environnement de test actuel. Gêne d'environnement, pas un bug du connecteur.
- 🟡 **Aucune vérification par compilateur réelle du C++ ni du bridge XPCOM (`lib.rs`)** de mon côté (Claude) — je vérifie l'équilibre des symboles et la cohérence avec le code source réel lu, mais chaque changement C++/bridge nécessite un vrai cycle de compilation pour être confirmé. Le reste du crate Rust (types, client, sous-modules) est vérifiable via un environnement de stubs local, avec un bon niveau de confiance.
- 🟡 **Aucun test automatisé** — contrairement à EWS (`EwsServer.sys.mjs`, `test_folder_sync.js`, `test_syncMessagesForFolder.js`), le connecteur Carbonio n'a pas encore de suite de tests `xpcshell`. Tout est validé manuellement via `./mach run` + profil de test.
