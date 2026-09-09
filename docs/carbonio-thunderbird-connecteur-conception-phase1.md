# Connecteur natif Carbonio pour Thunderbird — Conception Phase 1

**Statut** : document de conception — aucun code n'a été généré, conformément à la validation requise avant toute génération.
**Base d'analyse** : code source réel du module `mailnews/protocols/exchange/` (EWS/Graph) de `comm-central`, exploré via le miroir GitHub `mozilla/releases-comm-central`.
**Périmètre phase 1** : compte en lecture seule — authentification, synchronisation de la hiérarchie de dossiers, récupération des messages. Pas d'envoi, pas de suppression/déplacement, pas de calendrier/contacts.

---

## 1. Ce que Mozilla a réellement construit (et pourquoi c'est une bonne nouvelle)

Le module Exchange n'est pas un bloc monolithique spécifique à Exchange. Il est découpé en deux couches :

1. **Une couche C++ "coquille"** (`mailnews/protocols/exchange/`) qui s'intègre au framework mailnews existant (comptes, dossiers, filtres, recherche Gloda) en implémentant les mêmes interfaces que IMAP/POP3.
2. **Une couche Rust "moteur protocolaire"**, elle-même scindée en deux crates :
   - `rust/ews_xpcom` — logique **spécifique à Exchange/Graph** (requêtes SOAP/XML, endpoints).
   - `rust/protocol_shared` — infrastructure **générique, protocole-agnostique**, explicitement conçue pour être réutilisée par de futurs protocoles. Elle contient déjà des points d'extension pour un `calendar_listener` et un `event_listener`, ce qui suggère que Mozilla a anticipé l'ajout d'autres protocoles.

C'est cette seconde crate qui change la donne : une bonne partie du travail d'intégration XPCOM (listeners sûrs, gestion des erreurs, dispatch d'opérations async) n'a **pas** à être réécrite pour Carbonio.

## 2. Ce qui est réutilisable tel quel (`protocol_shared`)

| Fichier | Rôle | Réutilisable pour Carbonio ? |
|---|---|---|
| `client.rs` (trait `ProtocolClient`, `DoOperation<Client, Err>`) | Contrat générique : chaque opération (sync, fetch...) implémente `do_operation`, `into_success_arg`, `into_failure_arg` ; `handle_operation` gère le dispatch et les erreurs. | **Oui, directement.** C'est le point d'extension central. |
| `safe_xpcom/*` (folder_listener, message_sync_listener, message_fetch_listener, simple_operation_listener, uri, url_listener) | Wrappers sûrs autour des interfaces XPCOM de listeners (évite le C++ brut côté Rust). | **Oui, directement.** |
| `xpcom_io.rs` | Lecture de flux (`nsIInputStream`) côté Rust. | **Oui, directement.** |
| `operation_sender.rs` + `pref_based_server.rs` | Dispatch générique des opérations async vers le bon serveur/préférences. | **Oui, probablement avec adaptation mineure.** |
| `error.rs` (`ProtocolError`) | Type d'erreur générique protocole-agnostique. | **Oui.** |
| `authentication.rs` / `authentication_provider.rs` | Trait `AuthenticationProvider` avec 3 stratégies actuelles : Basic cleartext, NTLM (délégué à Necko), OAuth2. | **Partiellement — voir section 4, c'est le point de friction principal.** |

## 3. Ce qu'il faut créer pour Carbonio

### 3.1 Couche C++ (copie structurelle de `mailnews/protocols/exchange/src/`)

| Fichier EWS (référence) | Équivalent Carbonio à créer | Rôle |
|---|---|---|
| `ExchangeIncomingServer.h/.cpp` | `CarbonioIncomingServer.h/.cpp` | Sous-classe de `nsMsgIncomingServer`, point d'entrée du compte |
| `IExchangeIncomingServer.idl` | `ICarbonioIncomingServer.idl` | Interface XPIDL spécifique au serveur entrant |
| `ExchangeFolder.h/.cpp` | `CarbonioFolder.h/.cpp` | Représentation d'un dossier Carbonio |
| `IExchangeFolder.idl` | `ICarbonioFolder.idl` | Interface XPIDL du dossier |
| `ExchangeProtocolInfo.h/.cpp` | `CarbonioProtocolInfo.h/.cpp` | Métadonnées du protocole (port par défaut, capacités) |
| `ExchangeProtocolHandler.h/.cpp` | `CarbonioProtocolHandler.h/.cpp` | Enregistrement du schéma d'URI (`x-moz-carbonio`) |
| `ExchangeMessageSync.h/.cpp` | `CarbonioMessageSync.h/.cpp` | Synchro des messages d'un dossier |
| `ExchangeFetchMsgsToOffline.h/.cpp` | `CarbonioFetchMsgsToOffline.h/.cpp` | Récupération du corps des messages pour cache local |
| `ExchangeUrl.h/.cpp` | `CarbonioUrl.h/.cpp` | Construction des URLs internes |
| `EwsClient.h`, `IExchangeClient.idl` | `CarbonioClient.h`, `ICarbonioClient.idl` | Pont XPCOM vers le crate Rust |
| `components.conf` | `components.conf` (nouveau bloc) | Déclaration des CID/contract IDs — voir 3.3 |

*Hors périmètre phase 1 (à ne pas créer maintenant)* : `nsEwsOutgoingServer.h` (envoi), `ExchangeCopyMoveTransaction.*`, `ExchangeMessageCreate.*`, `ExchangeFolderCopyHandler.*` (déplacement/copie/création).

### 3.2 Couche Rust (nouveau crate `rust/carbonio_xpcom`)

Sur le modèle de `ews_xpcom`, mais en s'appuyant sur `protocol_shared` :

- `lib.rs` — implémentation de l'interface XPCOM `ICarbonioClient` (`#[xpcom::xpcom(implement(ICarbonioClient), atomic)]`), méthodes `Initialize`, `CheckConnectivity`, `SyncFolderHierarchy`, `GetMessage` (phase 1 uniquement).
- `client.rs` — struct `XpComCarbonioClient` implémentant `ProtocolClient`.
- `client/authenticate.rs` — **nouveau**, sans équivalent EWS direct (voir section 4).
- `client/sync_folder_hierarchy.rs` — appel à l'API Carbonio `GetFolderRequest`/`SyncRequest`, mappage vers `SafeExchangeFolderListener` (à renommer `SafeCarbonioFolderListener` ou générique).
- `client/sync_messages_for_folder.rs` — appel à `SyncRequest` (delta par token, avantage Carbonio par rapport à IMAP).
- `client/get_message.rs` — récupération du contenu MIME d'un message (`GetMsgRequest`).
- `response_parser.rs` — désérialisation des réponses Carbonio. **Avantage Carbonio** : le format `jsns` permet de désérialiser en JSON via `serde_json` plutôt qu'en XML, ce qui évite l'équivalent du crate `xml_struct` utilisé par EWS.
- `error.rs` — mapping des codes d'erreur SOAP Carbonio vers `ProtocolError`.

### 3.3 Enregistrement XPCOM (`components.conf`)

Nouveau bloc, calqué sur celui d'EWS, avec des `contract_ids` de type `?type=carbonio` :
```
@mozilla.org/messenger/server;1?type=carbonio
@mozilla.org/messenger/protocol/info;1?type=carbonio
@mozilla.org/mail/folder-factory;1?name=carbonio
@mozilla.org/messenger/messageservice;1?type=carbonio
@mozilla.org/network/protocol;1?name=carbonio  (scheme: x-moz-carbonio)
```
Pas de bloc `outgoing/server` en phase 1 (pas d'envoi).

### 3.4 Tests (sur le modèle de `mailnews/test/fakeserver/EwsServer.sys.mjs`)

Créer `mailnews/test/fakeserver/CarbonioServer.sys.mjs` : un faux serveur SOAP/JSON Carbonio pour les tests `xpcshell`, sur le modèle de `test_folder_sync.js` et `test_syncMessagesForFolder.js`. C'est la meilleure source pour comprendre le contrat exact attendu par le framework mailnews (quelles méthodes doivent être appelées, dans quel ordre, avec quels formats de retour).

## 4. Point de friction principal : le modèle d'authentification

`AuthenticationProvider::auth_header_value()` gère aujourd'hui trois cas : `passwordCleartext` (Basic), `NTLM` (délégué à Necko), `OAuth2` (bearer token via `msgIOAuth2Module`). Le modèle Carbonio (héritage Zimbra) est différent : un appel SOAP `AuthRequest` initial échange identifiant/mot de passe contre un **authToken** à durée de vie limitée, réutilisé ensuite soit en cookie, soit en en-tête custom.

Ça ne rentre dans aucune des trois cases existantes. Deux options :

- **A.** Ajouter un nouveau cas dans `auth_header_value()` (upstream, dans `protocol_shared`) — plus propre pour une future contribution à Mozilla, mais touche du code partagé avec EWS/Graph.
- **B.** Gérer l'échange de token entièrement côté client Carbonio (login au premier appel, cache du token, refresh à expiration), sans passer par ce mécanisme — plus isolé, plus rapide à prototyper, mais moins aligné avec le pattern existant.

Recommandation pour un POC : partir sur **B** pour aller vite, en gardant en tête que **A** serait le bon choix si l'objectif de contribution upstream se concrétise.

## 5. Prochaines étapes proposées

1. Valider ce découpage de fichiers (ce document).
2. Étudier en détail `ExchangeIncomingServer.cpp` et `test_folder_sync.js` pour extraire le contrat précis méthode par méthode (ce qui alimentera un futur document "phase 1 - spécification détaillée").
3. Une fois validé, scaffold du crate Rust `carbonio_xpcom` (avec ton feu vert explicite avant toute génération de code, comme convenu).
