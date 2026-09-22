# Procédure — ajouter un nouveau type de connexion dans l'assistant de compte Thunderbird (Account Hub)

**Statut** : procédure établie à partir de l'implémentation réelle du support Carbonio
(`patches/add-carbonio-dedicated-account-wizard-screen.patch`). Patch rédigé, pas encore
testé en conditions réelles — voir `limitations-connues.md`, section "Compte et création".
Ce document sera mis à jour si le test révèle des ajustements nécessaires.

**Objectif** : servir de guide réutilisable si on doit un jour ajouter un autre type de
connexion (ou si quelqu'un d'autre doit comprendre/refaire ce travail), sans avoir à
reparcourir toute l'exploration de code qui a mené à cette procédure.

---

## Vue d'ensemble : les deux familles de chemins possibles

L'assistant de compte (`mail/components/accountcreation/`, dans `comm-central`) propose,
après le premier écran de sélection de protocole, **deux familles de chemins** selon la
nature du protocole :

1. **Chemin générique "entrant + sortant séparés"** (`manualConfigSubview` →
   `incomingConfigSubview` → `outgoingConfigSubview`) — pour les protocoles où le serveur
   d'envoi est distinct du serveur de réception : IMAP/POP3 + SMTP.
2. **Chemin dédié "point d'entrée unique"** — pour les protocoles où un seul serveur gère
   entrée et sortie : c'est le cas d'Exchange/EWS/Graph (`exchangeSettingsSubview` →
   `exchangeTypeSubview`, avec une étape d'auto-discovery), et c'est le modèle qu'on a
   suivi pour Carbonio (`carbonioSettingsSubview`, en une seule étape).

**Première décision à prendre avant de commencer** : dans laquelle de ces deux familles
le nouveau protocole s'inscrit-il ? Un protocole à serveur d'envoi séparé (type SMTP)
devrait rejoindre le chemin générique ; un protocole à point d'entrée unique (type API
propriétaire gérant tout) devrait avoir son propre écran dédié, sur le modèle Carbonio
ci-dessous plutôt que sur le modèle générique.

Cette procédure documente le **chemin dédié** (option 2), celui suivi pour Carbonio.

---

## Étape 0 — Prérequis côté backend

Avant de toucher à l'assistant, vérifier que le contrat XPCOM du serveur entrant est déjà
enregistré :

```
@mozilla.org/messenger/server;1?type=<votre-protocole>
```

Dans le `components.conf` du module du connecteur (voir `new-files/mailnews/protocols/carbonio/src/components.conf` pour l'exemple Carbonio). Si ce n'est pas le cas, **rien de ce qui suit ne créera un compte fonctionnel** — l'assistant peut construire la configuration et appeler `MailServices.accounts.createIncomingServer(username, hostname, type)`, mais si ce contrat n'existe pas, la création échouera silencieusement ou avec une erreur XPCOM.

Vérifier aussi `isExchangeConfig()` dans `AccountConfig.sys.mjs` — s'assurer que le nouveau
type n'y est pas testé par erreur (il ne devrait l'être que s'il s'agit réellement d'un
protocole de la famille Exchange).

## Étape 1 — Ajouter une carte sur l'écran de sélection de protocole

**Fichiers concernés** :
- `templates/accountHubEmailProtocolSelectFormTemplate.inc.xhtml` — ajouter un bloc
  `<label class="account-hub-protocol-select-card">` sur le modèle des cartes existantes
  (IMAP/Microsoft/POP3), avec un `value` unique sur l'`<input type="radio">`.
- `locales/en-US/messenger/accountcreation/accountHub.ftl` — ajouter l'entrée
  `account-hub-protocol-<votre-protocole>` référencée par `data-l10n-id` sur cette carte.

**Point de vigilance** : le titre de la carte doit être le nom du produit seul (voir
précédent "Microsoft", pas "Microsoft Exchange") — la précision technique va dans le
sous-titre (`account-hub-protocol-<...>` en `.ftl`), pas dans le titre affiché en dur dans
le XHTML.

## Étape 2 — Créer le widget et son template

**Deux nouveaux fichiers** :
- `content/widgets/email-<votre-protocole>-settings.mjs` — la classe du widget,
  héritant de `AccountHubStep`. Méthodes attendues par le contrat de l'orchestrateur :
  `connectedCallback()`, `setState(configData)`, `resetState()`, `captureState()`.
- `templates/accountHubEmail<VotreProtocole>SettingsTemplate.inc.xhtml` — le template XHTML.

**Deux styles possibles pour le template**, à choisir selon le nombre de champs :
- **Un seul champ** (ex. `email-exchange-settings.mjs`, juste une URL) → utiliser
  l'élément personnalisé `account-hub-input` directement dans le XHTML.
- **Plusieurs champs de nature différente** (hostname, port, sécurité, identifiant — notre
  cas) → reprendre le balisage brut déjà utilisé par `email-manual-incoming-form.mjs`
  (`<label data-l10n-id="...">` + `<input>` + icônes d'erreur/succès + `<span>` d'erreur),
  plus cohérent visuellement avec ce type de formulaire multi-champs déjà établi ailleurs
  dans l'assistant.

**Réutiliser les libellés Fluent génériques existants** avant d'en créer de nouveaux —
`account-hub-result-hostname-label`, `account-hub-hostname-error-text`,
`account-hub-on-port-label`, `account-hub-port-error-text`, `account-hub-ssl-label`,
`account-hub-select-security-warning`, `account-hub-result-username-label`,
`account-hub-username-error-text`, `account-hub-email-input` couvrent déjà hostname, port,
sécurité de connexion et nom d'utilisateur. Pour le cas Carbonio, seules deux nouvelles
entrées `.ftl` ont été nécessaires : le sous-titre de la carte, et le titre de l'écran
dédié (`account-hub-email-<votre-protocole>-settings`).

**Si le protocole n'a qu'un seul mode d'authentification** (comme Carbonio :
identifiant/mot de passe classique uniquement) : pas besoin de sélecteur de méthode
d'authentification dans le formulaire — fixer directement `config.incoming.auth` dans le
code du widget (voir `Ci.nsMsgAuthMethod.passwordCleartext` dans
`getCarbonioUserConfig()`).

**Si le protocole n'utilise pas SMTP pour l'envoi** (comme Carbonio, via une API native) :
ne pas ajouter de champs sortants au formulaire — copier simplement
`config.outgoing = config.incoming` dans le code, pour que rien en aval ne se retrouve
avec une config sortante vide ou invalide.

## Étape 3 — Enregistrer le nouveau widget dans le build

**Fichier** : `jar.mn` — ajouter une ligne pour le nouveau `.mjs`, juste à côté des autres
widgets `content/widgets/email-*.mjs`. (Les templates `.inc.xhtml`, eux, sont inclus par
préprocesseur, pas listés ici — voir étape 4.)

## Étape 4 — Enregistrer le template et l'élément dans le conteneur

**Fichier** : `templates/accountHubTemplate.inc.xhtml` — deux ajouts :
- une ligne `#include ./accountHubEmail<VotreProtocole>SettingsTemplate.inc.xhtml`, dans
  la liste des inclusions en tête de fichier (ordre alphabétique) ;
- un bloc `<email-<votre-protocole>-settings id="email<VotreProtocole>SettingsSubview" class="account-hub-step" title-id="account-hub-email-<votre-protocole>-settings" hidden="hidden"></email-<votre-protocole>-settings>`, au même niveau que les autres écrans (`email-exchange-settings`, etc.)

## Étape 5 — Câbler l'état dans l'orchestrateur (`views/email.mjs`)

C'est l'étape la plus dense, en 7 modifications distinctes du même fichier :

1. **Champ privé** pour la référence DOM (`#<votreProtocole>SettingsSubview;`), à côté des
   champs privés existants du même genre.
2. **Définition de l'état** dans l'objet `#states` :
   ```js
   <votreProtocole>SettingsSubview: {
     id: "email<VotreProtocole>SettingsSubview",
     nextStep: "emailPasswordSubview",       // ou une autre étape dédiée si besoin
     previousStep: "protocolSelectSubview",
     forwardEnabled: true,
     customActionFluentID: "",
     customForwardFluentID: "account-hub-email-connect-button",
     subview: {},
     templateId: "email-<votre-protocole>-settings",
   },
   ```
   **Point de vigilance** : prendre `manualConfigSubview` comme modèle pour une étape
   "normale" (bouton "Connect", pas de vérification en direct compliquée) — pas
   `exchangeSettingsSubview`, dont la forme (`forwardEnabled: false`,
   `customForwardFluentID: "account-hub-email-find-settings-button"`) est spécifiquement
   taillée pour déclencher une auto-discovery, sans rapport avec une étape de saisie
   manuelle directe.
3. **Câblage `querySelector`** dans `connectedCallback` (ou équivalent), sur le modèle
   exact des lignes existantes pour `#exchangeSettingsSubview`.
4. **Écouteur d'événement** `"config-updated"` sur ce même élément, ajouté au bloc
   d'écouteurs déjà existant.
5. **Gestion de l'état "actif"** — ajouter un `case "<votreProtocole>SettingsSubview":`
   dans le `switch` qui gère l'affichage de l'état courant (`this.#setCurrentConfigForSubview();`).
6. **Gestion de l'action "suivant"** — ajouter un `case` similaire dans le `switch` qui
   traite le clic sur le bouton d'action, en capturant `stateData.config` (pas `stateData`
   directement — `captureState()` renvoie `{ config, edited }`), puis
   `await this.#initUI(this.#states[this.#currentState].nextStep)`.
7. **Routage depuis l'écran de sélection**, dans `#initManualConfig` — ajouter un
   `case "<votre-protocole>":` dans le `switch (protocol)`, routant vers l'état créé à
   l'étape 2. **Attention** : ce `switch` n'a pas de cas par défaut silencieux — une valeur
   de `value` sur la carte (étape 1) qui ne correspond à aucun `case` ici lève une
   exception plutôt que d'échouer discrètement.

## Étape 6 — Générer et vérifier le patch

`comm-central` est un **dépôt Mercurial imbriqué**, séparé de `mozilla-central` — se
placer dans `~/Developement/thunderbird/source/comm` (pas `source/` seul) avant de lancer
`hg diff`. Pour les fichiers neufs, faire un `hg add` au préalable, sinon `hg diff` les
ignore silencieusement :

```bash
cd ~/Developement/thunderbird/source/comm
hg add mail/components/accountcreation/content/widgets/email-<votre-protocole>-settings.mjs
hg add mail/components/accountcreation/templates/accountHubEmail<VotreProtocole>SettingsTemplate.inc.xhtml
hg diff mail/components/accountcreation/ mail/locales/en-US/messenger/accountcreation/accountHub.ftl > /tmp/mon-patch.patch
```

Vérifier le patch obtenu contient bien **tous** les fichiers attendus, y compris le
`.ftl` (facile à oublier, il est en dehors du dossier `accountcreation/`).

## Étape 7 — Builder et tester

```bash
cd ~/Developement/thunderbird/source
./mach build
./mach run --profile <profil-de-test>
```

Ouvrir l'assistant de compte, vérifier que la nouvelle carte apparaît sur l'écran de
sélection de protocole, que le formulaire dédié s'affiche correctement à la sélection,
que les champs se valident comme attendu, et que la création de compte aboutit.

**Piège de recherche rencontré pendant ce travail, à éviter de reproduire** : en
environnement de développement local, les fichiers `.mjs`/`.xhtml` ne sont **pas**
recopiés/empaquetés dans `obj-.../dist/bin` sous une forme cherchable par `find` — ils
restent résolus directement depuis l'arbre source via le système de `chrome.manifest`. Si
un changement ne semble "pas pris en compte" après un rebuild, vérifier d'abord qu'on
regarde le **bon écran de l'assistant** (il y a plusieurs écrans distincts en cascade,
voir "Vue d'ensemble" ci-dessus) avant de soupçonner un problème de build ou de cache.

---

## Écueils rencontrés en construisant le cas Carbonio (pour référence)

- **Ne pas router un protocole à point d'entrée unique vers le chemin générique
  IMAP/POP3+SMTP** (`manualConfigSubview`) juste parce qu'il partage certains champs
  (hostname/port) — même si ça semble fonctionner de prime abord, ça n'a pas de sens
  structurel et pourrait exposer des champs/étapes (config SMTP) sans objet. Voir la
  discussion qui a mené à abandonner cette première approche, dans
  `limitations-connues.md`.
- **`email-exchange-settings.mjs` n'est pas un formulaire de configuration** — c'est un
  déclencheur d'auto-discovery à champ unique (URL de service). Ne pas le prendre comme
  modèle si le nouveau protocole n'a pas de mécanisme de détection automatique fiable.
- **`email-exchange-type.mjs` est spécifique à la complexité d'Exchange** (choix
  EWS/Graph/add-on, OAuth avec tenant/application ID, NTLM...) — ne pas le copier comme
  base pour un protocole plus simple ; en extraire seulement les briques d'interface
  partagées (`account-hub-input`, `account-hub-select`) plutôt que sa structure globale.
- **Un test numérique/seuil sur la valeur du protocole peut casser silencieusement avec un
  nouveau protocole** — repéré dans `email-manual-incoming-form.mjs` (`value >= 4` pour
  détecter "famille Exchange"), qui aurait mal classé Carbonio en `value="6"`. Préférer
  toujours un test explicite par liste de valeurs à un test par seuil, et vérifier tout le
  fichier pour ce genre de logique implicite avant de choisir une nouvelle valeur.
