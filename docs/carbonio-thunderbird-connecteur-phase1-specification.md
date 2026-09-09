# Connecteur Carbonio — Phase 1, spécification détaillée

Complète le document de conception initial. Basé sur la lecture directe de `ExchangeIncomingServer.cpp` et `test_folder_sync.js`. Toujours aucun code généré.

---

## 1. Contrat exact de la synchronisation de la hiérarchie de dossiers

`ExchangeIncomingServer::SyncFolderList()` construit un jeu de callbacks passés à un listener (`ExchangeFolderSyncListener`), puis appelle `client->SyncFolderHierarchy(listener, syncStateToken)`. Le crate Rust pilote ensuite ces callbacks au fil de la réponse serveur :

| Callback | Déclenché quand | Action côté C++ |
|---|---|---|
| `onNewRootFolder(id)` | Le serveur renvoie l'identifiant du dossier racine | Enregistre cet ID comme propriété du dossier racine local (`kExchangeIdProperty` → à renommer `kCarbonioIdProperty`) |
| `onFolderCreated(id, parentId, name, flags)` | Un dossier distant n'a pas d'équivalent local connu | `MaybeCreateFolderWithDetails` : cherche si l'ID existe déjà (idempotence), sinon crée le dossier via le message store, assigne l'ID, les flags (Inbox/Trash/etc.), notifie `nsIMsgFolderNotificationService` |
| `onFolderUpdated(id, parentId, name)` | Un dossier existant a changé de nom ou de parent | Compare nom/parent courants à ceux reçus ; si différents, appelle `LocalRenameOrReparentFolder` |
| `onFolderDeleted(id)` | Un dossier distant a disparu | Recherche le dossier local par ID, appelle `parent->PropagateDelete(folder, true)` (pas `DeleteSelf`, pour éviter de renvoyer une suppression au serveur) |
| `onSyncStateTokenChanged(token)` | Le serveur renvoie un nouveau token de sync incrémentale | Persisté comme propriété du serveur (`kSyncStateTokenProperty`), réutilisé au prochain sync pour ne récupérer que le delta |
| `onSuccess()` / `onError(status)` | Fin de l'opération | Réinitialise la barre de statut ; **dans les deux cas** appelle `postSyncCallback` (même en cas d'erreur partielle, pour ne pas bloquer les opérations suivantes comme la synchro des messages par dossier) |

**Point clé pour Carbonio** : l'identifiant distant d'un dossier n'est jamais stocké dans une table de correspondance séparée — c'est une simple propriété de chaîne (`SetStringProperty`/`GetStringProperty`) posée sur l'objet `nsIMsgFolder` local. La résolution ID → dossier (`FindFolderWithId`) fait un parcours en largeur (BFS) de l'arbre de dossiers à chaque fois. Simple à répliquer, mais à garder en tête si Carbonio a des milliers de dossiers (partages, etc.) — un index en cache pourrait devenir nécessaire plus tard, hors scope phase 1.

**Robustesse observée** : le token de sync est traité comme optionnel/résilient — s'il est absent ou invalide, on repart d'une chaîne vide (resync complet), jamais d'exception bloquante.

## 2. Contrat de test à répliquer (`test_folder_sync.js`)

Le test réel utilise :
- `EwsServer` (faux serveur, depuis `EwsServer.sys.mjs`) démarré en local, avec une URL de type `http://127.0.0.1:<port>/EWS/Exchange.asmx` — équivalent Carbonio : `CarbonioServer` exposant une URL type `http://127.0.0.1:<port>/service/soap` (ou l'endpoint réel Carbonio).
- `localAccountUtils.create_incoming_server("ews", port, "user", "password")` — le premier argument est le `type` déclaré dans `components.conf` (`?type=ews`) → deviendra `"carbonio"`.
- Le déclenchement du sync se fait via l'appel utilitaire `syncFolder(incomingServer, folder)`, qui encapsule `nsIMsgFolder::GetNewMessages` — **pas un appel direct à l'API du client**. C'est important : ça veut dire que le point d'entrée public à exposer est bien au niveau du framework mailnews standard, pas une méthode custom.
- Scénarios à répliquer tels quels pour Carbonio : création initiale de la hiérarchie, apparition d'un nouveau dossier distant, suppression distante, renommage (l'ID doit être préservé), déplacement/reparent (l'ID doit être préservé), reparent d'un sous-arbre entier à plusieurs niveaux.

Ces scénarios constituent une bonne checklist d'acceptation pour la phase 1 : **si ces 6 scénarios passent avec un faux serveur Carbonio, la synchro de hiérarchie est fonctionnellement correcte.**

## 3. Authentification — Option A (retenue) : extension propre du mécanisme partagé

`nsMsgAuthMethodValue` est un simple entier déclaré dans `mailnews/base/public/MailNewsTypes2.idl` :

```
const nsMsgAuthMethodValue passwordCleartext = 3;
const nsMsgAuthMethodValue passwordEncrypted = 4;
const nsMsgAuthMethodValue GSSAPI = 5;
const nsMsgAuthMethodValue NTLM = 6;
```

**Plan concret pour l'option A :**

1. Ajouter une nouvelle constante, par exemple `const nsMsgAuthMethodValue carbonioToken = 8;` (valeur suivante disponible à vérifier au moment de l'implémentation — le fichier peut évoluer).
2. Dans `protocol_shared/src/authentication/authentication_provider.rs`, ajouter un nouveau bras dans le `match` de `auth_header_value()` :
   - Effectue (ou réutilise depuis un cache) l'appel de login Carbonio pour obtenir un `authToken`.
   - Gère le TTL/refresh du token (Carbonio expire généralement les tokens après une durée fixe — à vérifier précisément côté API Carbonio).
   - Retourne la valeur formatée attendue par l'API Carbonio (probablement un cookie ou un header custom plutôt qu'un simple `Authorization: Bearer` — **point à confirmer avec la doc API Carbonio avant l'implémentation**, car ça détermine si on peut réutiliser tel quel le mécanisme `auth_header_value` -> header HTTP, ou s'il faut un point d'extension supplémentaire pour injecter un cookie).
3. Le cache/refresh du token peut vivre dans le client Carbonio lui-même (`XpComCarbonioClient`), pas dans le trait partagé — le trait se contente d'exposer le résultat final.

**Ce que je ne peux pas trancher sans info Carbonio-side** : le token Carbonio est-il transmis en cookie de session ou en en-tête HTTP dédié ? Ça détermine si l'option A telle que décrite ci-dessus suffit, ou s'il faut aussi un point d'extension pour la gestion des cookies (auquel cas il faudrait regarder comment EWS gère `maybe_set_necko_auth_cache` pour NTLM comme modèle, puisque ce mécanisme passe par le cache d'authentification de Necko plutôt que par un simple header).

## 4. Résultats empiriques (validés via `carbonio_explorer.py` contre une instance réelle)

### 4.1 Authentification

- Requête : `POST https://<host>/service/soap`, `AuthRequest` (`urn:zimbraAccount`) avec `account: {by: "name", _content: <email>}` et `password`.
- Réponse : `AuthResponse.authToken[0]._content` (le token), `AuthResponse.lifetime` en millisecondes (confirmé : ~172 799 990 ms, soit ~48h).
- **Transport confirmé** : le token va dans `Header.context.authToken` (chaîne simple) de l'enveloppe JSON de **chaque requête suivante** — pas un header HTTP, pas un cookie (le cookie `ZM_AUTH_TOKEN` n'est utilisé que côté API REST, non utilisée ici). Confirme la remarque de la section 3 : la construction de la requête doit se faire au niveau du corps JSON, pas via un mécanisme de header HTTP générique.
- Pas de endpoint de refresh testé/observé — hypothèse de travail : ré-authentifier avec identifiant/mot de passe à l'expiration (comportement à confirmer plus tard, non bloquant pour la phase 1).

### 4.2 `GetFolderRequest` (état complet, non incrémental)

- Renvoie une **arborescence imbriquée** (chaque dossier a un tableau `folder` pour ses enfants), avec malgré tout un champ `l` (locationId du parent) sur chaque nœud.
- Un seul namespace de dossiers pour mail/calendrier/contacts/tâches, distingué par le champ `view` (`"message"`, `"appointment"`, `"contact"`, absent pour Inbox/Trash/certains dossiers système). **Le filtrage sur `view` vide/absent seul n'est pas fiable** (retour de Julien) — préférer une combinaison IDs système connus + liste explicite de `view` à exclure.
- Les dossiers partagés (autres comptes) apparaissent séparément dans un tableau **`link`**, avec `zid`/`rid`/`owner` — hors périmètre phase 1.
- **IDs système fixes et stables** : `1`=USER_ROOT, `2`=Inbox, `3`=Trash, `4`=Junk, `5`=Sent, `6`=Drafts, `11`=racine technique (parent de USER_ROOT, Tags, Conversations, Comments). Bien plus simple à mapper que les flags EWS.

### 4.3 `SyncRequest` — deux formes bien distinctes selon le contexte

- **Sans `token` (sync initial)** : renvoie l'état complet, arborescence imbriquée comme `GetFolderRequest`, **plus** les IDs de tout le contenu (messages `m.ids`, conversations `c.ids`, contacts `cn.ids`, rendez-vous `appt.ids`, tags) directement attachés à chaque dossier. Un seul appel suffit pour amorcer tout l'état initial.
- **Avec `token` (delta)** : renvoie une **liste plate** de dossiers modifiés (champ `l` = parent direct, pas d'imbrication). **Confirmé empiriquement** : une création suivie d'un renommage entre deux syncs ne produit **qu'une seule entrée** représentant l'état final — `SyncRequest` donne un état courant, pas un journal d'événements. La distinction création/mise à jour se fait donc côté client, exactement comme `FindFolderWithId` le fait pour EWS (id connu localement → update, sinon → create).
- **Déplacement (y compris "corbeille")** : traité comme une entrée `folder` normale avec un nouveau `l` — pas de distinction structurelle entre renommage et déplacement, cohérent avec ce qu'EWS traite aussi de façon unifiée (`LocalRenameOrReparentFolder`).
- **Suppression définitive** : apparaît dans un tableau `deleted` séparé. **Sans `typed`**, c'est une liste plate d'IDs mélangeant tous types d'objets (`{"ids": "1229,704"}`) — inutilisable en l'état pour distinguer un dossier supprimé d'un message supprimé. **Avec `typed: 1`**, confirmé : `deleted` se décompose par type (`deleted.folder.ids` isole les IDs de dossiers), avec le champ `ids` générique conservé en plus pour compatibilité. **Conclusion : `typed: 1` est indispensable pour la phase 1**, à inclure systématiquement dans le `SyncRequest` du futur client Rust.
- Le champ `token` de la réponse change de représentation JSON selon le contexte (entier ou chaîne) — à traiter comme une valeur opaque côté parsing, ne jamais supposer un type fixe.

## 5. Statut

Le contrat de synchronisation de la hiérarchie de dossiers est maintenant **validé empiriquement** sur les points suivants : authentification et transport du token, forme initiale vs delta, création/mise à jour/déplacement/suppression. Les inconnues restantes (refresh de token à expiration, comportement à grande échelle avec de nombreux dossiers) ne sont pas bloquantes pour un POC phase 1 et pourront être traitées au fil de l'implémentation.

Prêt à passer au scaffold du crate `carbonio_xpcom` et des fichiers C++ associés (validation explicite requise avant génération, comme convenu).
