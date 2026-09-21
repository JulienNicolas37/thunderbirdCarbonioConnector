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
  - **Précision technique (en cours d'investigation)** : le formulaire de configuration manuelle (`email-manual-incoming-form.mjs`) code en dur la liste des protocoles disponibles dans un dictionnaire numérique (`{1: "imap", 2: "pop3", 3: "exchange", 4: "ews", 5: "graph"}`), à deux endroits du fichier. Ajouter Carbonio nécessitera donc un patch sur `comm-central` lui-même (pas seulement du code côté Carbonio), dans la même veine que les patches déjà faits sur `mozilla-central` pour l'ouverture de message. Bonne nouvelle : la configuration Carbonio (hostname/port/identifiants classiques) correspond au chemin "par défaut" du formulaire (celui utilisé par IMAP/POP3), pas au chemin spécial "Exchange" (URL dédiée) — le patch nécessaire semble donc limité en ampleur.
  - **Référence de parité, côté écrans EWS** : l'assistant de compte Thunderbird propose, pour un compte Exchange/Microsoft, plusieurs méthodes de connexion au choix — IMAP, POP3, Exchange, Microsoft Graph API, ou Exchange (avec add-on). Bien qu'EAS (Exchange ActiveSync) soit également techniquement configurable, ce n'est pas dans cette liste de choix visibles à l'écran — et de toute façon jugé superflu à répliquer pour Carbonio (voir ci-dessous). À garder comme référence de la pluralité de méthodes déjà gérée par l'écran EWS, pour calibrer ce qu'on doit viser côté Carbonio : au minimum IMAP, POP3 et SMTP (déjà configurables manuellement aujourd'hui) en plus du protocole natif Carbonio lui-même.
  - **Décision explicite** : pas de support EAS (Exchange ActiveSync) pour Carbonio — techniquement possible mais jugé inutile à couvrir.
- 🔴 **Pas d'autodiscover dédié à Carbonio** — reste à trancher : un programme à déployer sur les frontaux Carbonio (répondant à des requêtes DNS/HTTP standardisées) associé à une configuration DNS sur le domaine du client, ou la réutilisation du standard EAS existant côté client Thunderbird. Dans les deux cas, prévoir la résolution DNS associée au domaine du client comme point de départ.
- 🟢 **Pas d'auto-discovery de la configuration serveur** — contrairement à EAS/Exchange (`ExchangeAutoDiscover.sys.mjs`, protocole propriétaire Microsoft type `autodiscover.<domaine>`), aucun mécanisme équivalent n'a été envisagé pour Carbonio. Le contexte majoritairement On-Premise rend une heuristique par déduction du nom de domaine peu fiable (exemple concret : le compte de test a pour adresse `@carboniocloud.fr` mais un serveur réel `mail.carboniocloud.fr` — pas le même nom d'hôte). Un vrai mécanisme ne vaudrait la peine que si Carbonio expose lui-même un endpoint de découverte standardisé côté serveur — à vérifier auprès de la documentation produit Carbonio avant d'envisager quoi que ce soit ici. Voir le point 🔴 ci-dessus, qui en fait désormais un objectif explicite plutôt qu'une simple piste de confort.
- 🟢 **Pas de support SAML ou 2FA via un fournisseur d'identité tiers à Carbonio** (ex. LemonLDAP, OIDC) — l'authentification actuelle suppose un couple identifiant/mot de passe classique géré directement par Carbonio (voir section Authentification). Pertinent pour les déploiements où l'authentification est déléguée à un IdP externe.

## Hors périmètre phase 1 (rappel, pas des bugs)

- Calendrier, contacts, tâches, chats — jamais synchronisés, exclus par le filtre mail/non-mail.
- Envoi de messages, suppression, déplacement, copie de messages.

## Environnement de développement / process

- 🟡 **Deux patches touchent le cœur `mozilla-central`, pas seulement `comm-central`** — `patches/allow-x-moz-carbonio-in-process-isolation.patch` (`dom/ipc/ProcessIsolation.cpp`) et `patches/allow-x-moz-carbonio-in-parent-process.patch` (`docshell/base/nsDocShell.cpp`). Contrairement aux autres patches du projet, ceux-ci seront écrasés par un `hg pull -u`/mise à jour du checkout mozilla-central et devront être réappliqués manuellement à chaque fois, jusqu'à ce qu'ils soient proposés et acceptés upstream (voir `docs/mecanique-ouverture-message.md`, section "Pour une contribution upstream"). **À suivre** : vérifier qu'ils s'appliquent encore proprement après chaque mise à jour du checkout.
- 🟢 **Sandbox Thunderbird** — `CanCreateUserNamespace() unshare(CLONE_NEWPID): EPERM` nécessite `MOZ_DISABLE_CONTENT_SANDBOX=1` dans l'environnement de test actuel. Gêne d'environnement, pas un bug du connecteur.
- 🟡 **Aucune vérification par compilateur réelle du C++ ni du bridge XPCOM (`lib.rs`)** de mon côté (Claude) — je vérifie l'équilibre des symboles et la cohérence avec le code source réel lu, mais chaque changement C++/bridge nécessite un vrai cycle de compilation pour être confirmé. Le reste du crate Rust (types, client, sous-modules) est vérifiable via un environnement de stubs local, avec un bon niveau de confiance.
- 🟡 **Aucun test automatisé** — contrairement à EWS (`EwsServer.sys.mjs`, `test_folder_sync.js`, `test_syncMessagesForFolder.js`), le connecteur Carbonio n'a pas encore de suite de tests `xpcshell`. Tout est validé manuellement via `./mach run` + profil de test.
