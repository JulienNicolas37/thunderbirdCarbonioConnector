# Patches — à quoi sert chacun, et pourquoi

**Convention adoptée à partir de `006`** : l'en-tête d'un patch reste bref (quel
problème, correctif en une phrase, renvoi vers ce document) — le récit complet du
diagnostic, les pistes écartées et le contexte de découverte vivent ici, pas dans
le patch lui-même. Les patches antérieurs (`003`-`005`) ont encore de longs
en-têtes ; ils seront alignés sur ce modèle au moment de la rationalisation
générale des patches (voir `limitations-connues.md`, section "Environnement de
développement / process").

---

## `0001-register-carbonio-protocol-dir.patch`

**Fichier** : `mailnews/protocols/moz.build`
**Rôle** : ajoute `carbonio/src` à la liste des répertoires compilés par le
système de build de `comm-central`, aux côtés de `common`, `exchange/public`,
`exchange/src`. Sans cette ligne, notre code C++ (`CarbonioService`,
`CarbonioIncomingServer`, `CarbonioFolder`...) ne serait tout simplement jamais
compilé. Le tout premier patch nécessaire pour que le module existe.

## `0002-register-carbonio-xpcom-in-gkrust.patch`

**Fichier** : `rust/gkrust/src/lib.rs`
**Rôle** : ajoute `extern crate carbonio_xpcom as _;` à la liste des crates Rust
liées dans `libxul`, aux côtés d'`ews_xpcom`/`graph_xpcom`. Sans cette ligne, le
crate `carbonio_xpcom` (notre pont XPCOM vers le client Rust) ne serait jamais
lié au binaire final, même s'il compile correctement de son côté.

## `003-allow-x-moz-carbonio-in-parent-process.patch`

**Fichier** : `docshell/base/nsDocShell.cpp`
**Rôle** : ajoute `x-moz-carbonio` à la liste blanche de schémas d'URI autorisés
à rester dans le processus parent, dans `CanLoadInParentProcess()` — la même
liste où figurent déjà `imap`, `mailbox`, `x-moz-ews`, etc.
**Pourquoi** : première moitié du correctif à la boucle infinie d'ouverture de
message (`NS_ERROR_DOCUMENT_LOAD_LISTENER_NO_PARENT_CHANNEL`). Ce patch seul
**ne suffit pas** — il ne couvre que le sens retour (un chargement qui revient
d'un processus de contenu vers le parent), pas le sens qui nous concernait
réellement. Voir `004` pour le correctif décisif, et `docs/mecanique-ouverture-message.md`
pour le récit complet de cette investigation (la plus longue et la plus dense
de tout le projet à ce jour).

## `004-allow-x-moz-carbonio-in-process-isolation.patch`

**Fichier** : `dom/ipc/ProcessIsolation.cpp`
**Rôle** : ajoute `x-moz-carbonio` à une seconde liste blanche, dans
`IsolationBehaviorForURI()` — celle qui décide réellement, à partir du seul
schéma d'URI, si un chargement doit être isolé dans un processus de contenu
séparé ou rester dans le parent.
**Pourquoi (le correctif décisif)** : sans cette entrée, tout chargement de
message Carbonio déclenchait un vrai changement de processus, provoquant
l'échec `NS_ERROR_DOCUMENT_LOAD_LISTENER_NO_PARENT_CHANNEL` et la boucle
infinie de `loadMessage()`. `x-moz-ews` figure déjà dans cette liste, ce qui
explique pourquoi EWS n'a jamais rencontré ce problème. Détail complet du
cheminement (comparaison EWS/Carbonio en conditions réelles, résolution de
piles d'appel, lecture du code source Gecko local) : `docs/mecanique-ouverture-message.md`.

## `005-add-carbonio-dedicated-account-wizard-screen.patch`

**Fichiers** : nouveau widget `email-carbonio-settings.mjs` + template associé,
plus modifications de `views/email.mjs`, `accountHubEmailProtocolSelectFormTemplate.inc.xhtml`,
`accountHubTemplate.inc.xhtml`, `jar.mn`, `accountHub.ftl`.
**Rôle** : ajoute une carte "Carbonio" dédiée sur l'écran de sélection de
protocole de l'assistant de création de compte, menant à un formulaire à une
seule étape (hostname/port/sécurité/identifiant — pas de champ mot de passe,
géré par l'étape partagée qui suit).
**Pourquoi un écran dédié plutôt que le formulaire manuel générique
(IMAP/POP3+SMTP)** : Carbonio est un protocole à point d'entrée unique (un seul
serveur gère entrée et sortie, via son API SOAP/REST native — voir
`limitations-connues.md`, "Hors périmètre phase 1"). Thunderbird a déjà tranché
cette question dans le même sens pour Exchange, qui a son propre écran dédié
plutôt que de passer par le formulaire générique. Contient aussi, à titre
historique, les modifications de la première approche abandonnée (ajout de
Carbonio comme sixième option du menu déroulant du formulaire manuel
générique) — inoffensives mais plus jamais atteignables dans l'UI une fois cet
écran dédié en place.
**Testé** : la carte apparaît et route correctement vers l'écran dédié.

## `006-add-carbonio-endpoint-url-construction.patch`

**Fichiers** : `AccountConfig.sys.mjs`, `ConfigVerifier.sys.mjs`, `CreateInBackend.sys.mjs`.
**Rôle** : construit et enregistre la préférence serveur `carbonio_url` (l'URL
SOAP complète, ex. `https://mail.example.com/service/soap`) au moment de la
création de compte, et déclare Carbonio comme protocole à point d'entrée unique
auprès du reste de l'assistant.

**Découvert en testant `005`** : la création de compte échouait à l'étape mot
de passe avec `0x80070057 (NS_ERROR_ILLEGAL_VALUE) [nsIMsgIncomingServer.verifyLogon]`.

**Diagnostic** : l'écran dédié de `005` ne collecte que hostname/port/sécurité/
identifiant — rien qui renseigne `carbonio_url`, la préférence que
`CarbonioIncomingServer::GetProtocolClient()` lit pour construire le client
Rust (`GetCarbonioUrl`/`Initialize`, dans `CarbonioIncomingServer.cpp` — l'URL
est transmise telle quelle, sans construction de chemin côté C++ ou Rust).
`carbonio_xpcom`'s `soap.rs` documente le format attendu : l'URL complète,
chemin `/service/soap` inclus, confirmé empiriquement — voir le doc de spec
phase 1. Sans cette préférence, le client Rust reçoit un endpoint vide, échoue
à parser l'URL, et l'échec remonte au premier point du flux qui sollicite
réellement le client : `verifyConfig() → verifyLogon() → GetProtocolClient() → Initialize()`.

**Deux fichiers distincts posent ce genre de préférence spécifique au
protocole**, chacun de son côté, avant d'appeler `verifyLogon()`/de finaliser
la création : `CreateInBackend.sys.mjs` (création réussie sans interaction) et
`ConfigVerifier.sys.mjs` (la vérification en direct — celui réellement emprunté
par le flux de l'assistant). Les deux avaient déjà le même schéma pour
`owa_url`/`ews_url`/`eas_url`, et il manquait l'équivalent Carbonio dans les
deux — corriger un seul des deux aurait laissé l'autre chemin cassé.

**Centralisation plutôt que duplication** : la construction de l'URL vit une
seule fois, dans `AccountConfig.prototype.buildCarbonioEndpointURL()`, aux
côtés d'`isExchangeConfig()` — les deux fichiers concernés importent déjà
`AccountConfig`. Une première version de ce patch comparait directement
`config.incoming.type == "carbonio"` à trois endroits distincts ; retravaillée
en `AccountConfig.prototype.isCarbonioConfig()`, sur le modèle d'`isExchangeConfig()`,
suite à une remarque de Julien : EWS centralise ce genre de test dans une
méthode nommée plutôt que de répéter la comparaison littérale, et il valait
mieux suivre cette convention déjà établie plutôt que d'en réinventer une
nouvelle en silence (voir `limitations-connues.md`, "Auditer mes propres
écarts de méthodologie...").

**Second bug trouvé dans la foulée** : une fois `carbonio_url` correctement
renseignée, la création échouait à l'étape suivante avec `"No outgoing server:
inconsistent flags"`. Cause : `configureOutgoingFromIncoming()` — qui
détermine si les flux d'authentification sans mot de passe (OAuth,
GSSAPI, "sans mot de passe") traitent entrée et sortie comme un seul serveur
combiné — ne renvoyait `true` que pour `isExchangeConfig()`. Notre widget fait
bien `config.outgoing = config.incoming`, mais rien ne déclarait ce choix au
reste du système, d'où l'incohérence. Corrigé en ajoutant `isCarbonioConfig()`
à cette même condition.

**Point non confirmé, à surveiller** : le support du HTTP non chiffré (choix
"Aucun chiffrement" dans le formulaire) n'est validé que contre le harnais de
test local (`CarbonioServer`), jamais contre un vrai déploiement Carbonio.
Gardé pour que ce choix ait un effet réel plutôt que d'être silencieusement
ignoré, mais c'est une extrapolation, pas un fait vérifié.

**Statut** : testé, corrige bien les deux erreurs rencontrées. À revalider
après chaque évolution du formulaire `005`.
