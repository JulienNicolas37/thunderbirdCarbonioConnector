# Mécanique d'ouverture d'un message — du double-clic à l'affichage

Ce document retrace, étape par étape, tout ce qui se passe quand on double-clique
sur un message dans la liste, et documente précisément où notre connecteur bloque
aujourd'hui. Il sert de base de travail interne, et de matière première pour un
futur échange avec la communauté Thunderbird/Gecko.

**Statut : non résolu.** Voir la section [Où ça bloque](#où-ça-bloque) et
[Ce qu'on a écarté](#ce-quon-a-écarté) pour l'état exact des investigations.

---

## Vue d'ensemble du chemin

```
1. Double-clic (about3Pane.js)
        │
        ▼
2. aboutMessage.js → résolution du service (XPCOM, générique)
        │
        ▼
3. CarbonioService::LoadMessage (notre code)
        │
        ▼
4. docShell.LoadURI() → bascule dans la mécanique interne Gecko/Fission
        │
        ▼
5. DocumentChannel (placeholder) → DocumentLoadListener (processus parent)
        │
        ▼
6. CarbonioProtocolHandler::NewChannel → CarbonioMessageChannel (le "vrai" canal)
        │
        ▼
7. CarbonioMessageChannel::AsyncOpen (notre code)
        │
        ├─ (si déjà en cache) StartMessageReadFromStore
        └─ (sinon) DownloadMessageAndReadFromStore → Rust/SOAP → store local
        │
        ▼
8. AsyncReadMessageFromStore (helper partagé, commun avec EWS)
        OnStartRequest → OnDataAvailable → OnStopRequest
        │
        ▼
        ❌ BLOCAGE ICI — voir plus bas
```

---

## Étape par étape, en détail

### 1. Le déclenchement (JS, hors de notre code)

`about3Pane.js` détecte le double-clic sur la ligne du message, récupère l'en-tête
sélectionné, et lance `MsgOpenSelectedMessages()`, qui aboutit dans
`chrome://messenger/content/aboutMessage.js`.

### 2. Comment `aboutMessage.js` trouve *notre* service

`aboutMessage.js` ne connaît rien de "Carbonio" spécifiquement — il est
**complètement générique**. Le mécanisme :

1. Il regarde le **schéma** de l'URI du message (`carbonio-message://...`).
2. Il construit dynamiquement un identifiant de contrat XPCOM :
   `@mozilla.org/messenger/messageservice;1?type=carbonio-message`
   (fonction `MailServices.messageServiceFromURI()` / `GetMessageServiceFromURI`
   côté C++).
3. Le gestionnaire de composants XPCOM (`nsComponentManager`), qui a indexé au
   démarrage tous les `components.conf` de tous les modules (EWS, IMAP, NNTP,
   Carbonio...), retrouve que ce contrat correspond à notre `CarbonioService`
   (déclaré côté nous avec `"contract_ids": ["@mozilla.org/messenger/messageservice;1?type=carbonio-message"]`),
   l'instancie (ou réutilise l'instance existante), et la renvoie.
4. `aboutMessage.js` appelle alors :
   ```js
   messageService.loadMessage(messageUri, docShell, msgWindow, urlListener, autodetectCharset)
   ```

C'est ce qui permet à Thunderbird d'ajouter de nouveaux types de comptes sans
jamais toucher au code du panneau de lecture.

### 3. `CarbonioService::LoadMessage` (notre code, `CarbonioService.cpp`)

- Transforme l'URI `carbonio-message://.../Inbox#1` en `x-moz-carbonio://.../Inbox/1`
  (via `GetUrlForUri`)
- Construit un **`nsDocShellLoadState`** — voir l'encart ci-dessous
- Appelle `aDisplayConsumer->LoadURI(loadState, false)` — remet la main au docShell

> **Qu'est-ce qu'un `nsDocShellLoadState` ?**
> C'est une "fiche de commande" de navigation : un objet qui décrit ce qu'il faut
> charger et comment, avant que le chargement ne commence. Il regroupe :
> - l'URI à charger
> - les drapeaux de chargement (`LoadFlags` — ex. "ne mets pas ça dans
>   l'historique") ; chez nous : aucun drapeau spécial
> - le **principal déclencheur** (`TriggeringPrincipal`) — *qui* demande ce
>   chargement, utilisé pour les vérifications de sécurité (CSP...). On utilise
>   le "principal système" puisque c'est Thunderbird lui-même qui charge le
>   message
> - `FirstParty` — indicateur de confidentialité (cookies tiers), peu pertinent
>   pour un message mais fait partie du contrat standard
>
> **Vérifié : notre construction de ce `loadState` est identique, champ pour
> champ, à celle d'EWS.** Ce n'est donc pas une différence de configuration à ce
> niveau qui explique le blocage.

### 4-5. La sélection de processus (Fission) — ce qu'on ne contrôle pas

C'est la découverte la plus importante de nos investigations. `docShell.LoadURI()`
**n'ouvre jamais directement** notre canal. À la place :

- Gecko crée un **`DocumentChannel`**, un canal *placeholder* générique, juste
  pour que le docShell sache "un chargement est en cours"
- Ce placeholder transmet tout à un **`DocumentLoadListener`**, qui tourne dans
  le **processus principal** (doc officielle, `netwerk/ipc/DocumentChannel.cpp`) :
  > *"DocumentChannel is a protocol agnostic placeholder nsIChannel
  > implementation that we use so that nsDocShell knows about a connecting
  > load. It transfers all data into a DocumentLoadListener (running in the
  > parent process), which will create the real channel for the connection,
  > and decide which process to load the resulting document in. If the
  > document is to be loaded in the current process, then we'll synthesize a
  > redirect replacing this placeholder channel with the real one, otherwise
  > the originating docshell will be removed during the process switch."*
- Ce `DocumentLoadListener` est celui qui va **réellement créer notre canal**,
  attendre sa réponse, et décider **dans quel processus** afficher le résultat
- Une fois la décision prise, il **remplace** le placeholder par le vrai canal
  via une "redirection" simulée

**Ce comportement est normal et systématique** : il se produit pour *toute*
navigation `docShell.LoadURI()`, mail ou page web, EWS comme Carbonio. C'est ce
qui explique qu'on observe plusieurs `AsyncOpen` et plusieurs processus (PID)
différents à chaque clic, chez nous **et** chez EWS — ce n'est pas en soi un bug.

### 6. `CarbonioProtocolHandler::NewChannel` — le "vrai" canal

Crée `new CarbonioMessageChannel(uri)`. Ce qui fait de cet objet **le** canal que
`DocumentLoadListener` attend, ce n'est pas l'instanciation en elle-même, mais
trois choses qui viennent avec :

1. **Un contrat d'interfaces à respecter**, pas juste à déclarer : notre classe
   hérite de `nsMailChannel`, `nsIChannel`, `nsHashPropertyBag` et implémente
   `nsIRequest`. `DocumentLoadListener` va **appeler** ces méthodes
   (`AsyncOpen`, `GetStatus`, `IsPending`, `Cancel`...) et s'attend à un
   comportement précis en retour.
2. **Un état injecté par l'extérieur** : `nsILoadInfo` (donné dès la
   construction), le type de contenu (`mContentType`), le groupe de chargement,
   les callbacks de notification (`SetLoadGroup`/`SetNotificationCallbacks`).
   Notre canal doit conserver et honorer ces informations tout du long.
3. **Une référence tenue en mémoire pendant toute la durée du chargement** :
   `DocumentLoadListener` obtient **une seule fois** un pointeur vers notre
   objet et le **garde**, pour l'utiliser à plusieurs reprises (appeler
   `AsyncOpen`, puis plus tard faire la "redirection" finale vers le bon
   processus). Ce point est la clé du blocage actuel (voir plus bas).

### 7. `CarbonioMessageChannel::AsyncOpen` (notre code)

- Vérification CSP (`nsContentSecurityManager::doContentSecurityCheck`)
- Résout l'URI en en-tête via `CarbonioService::MessageURIToMsgHdr`
- Si déjà en cache (flag `Offline` posé) → `StartMessageReadFromStore()`
- Sinon → `DownloadMessageAndReadFromStore()` :
  - `CarbonioIncomingServer::GetProtocolClient` → pont XPCOM Rust
  - `client->GetMessage(...)` → requête SOAP réelle, récupération du MIME brut
  - Écriture dans le store local (mbox), pose du flag hors-ligne
  - Rappel de `StartMessageReadFromStore()`

### 8. `StartMessageReadFromStore` → `AsyncReadMessageFromStore`

Helper **partagé avec EWS** (`OfflineStorage.cpp`). Censé appeler, dans l'ordre,
sur le vrai listener (le panneau de lecture) :
`OnStartRequest` → `OnDataAvailable` (les octets, un ou plusieurs appels) →
`OnStopRequest` (terminé, avec un code de statut).

---

## Où ça bloque

Les étapes 1 à 7 fonctionnent **intégralement** : le message est correctement
récupéré (octets confirmés en logs), le canal est bien créé et démarré.

À l'étape 8 : `OnStartRequest` s'appelle bien, avec la **même identité de canal**
tout du long (vérifié par instrumentation — pas de confusion entre deux canaux
différents). Mais **`OnDataAvailable` n'est jamais appelé** — aucune donnée
n'est jamais livrée au panneau de lecture. On tombe directement sur
`OnStopRequest` avec le code :

```
NS_ERROR_DOCUMENT_LOAD_LISTENER_NO_PARENT_CHANNEL = 0x804B004F
```

(confirmé via le fichier généré `ErrorList.h`)

Ce nom est extrêmement parlant : il vient très probablement de
`netwerk/ipc/DocumentLoadListener.cpp`, et signifie que **quelque chose essaie
de référencer "le vrai canal" après que `DocumentLoadListener` en a déjà perdu
la trace**.

**Conséquence côté UI** : le panneau de lecture ne recevant jamais rien,
`aboutMessage.js` relance `loadMessage()` depuis le tout début (étape 1) avec un
nouveau `nsIUrlListener` — d'où la boucle infinie observée. Chaque nouvelle
tentative d'`AsyncOpen` se fait dans un **processus enfant différent** (PID
confirmé changeant à chaque cycle), alors que `LoadMessage` lui-même reste
toujours dans le processus parent.

**Chez EWS**, la même mécanique (étapes 4-6) se déroule aussi (plusieurs
`AsyncOpen`, plusieurs processus par clic), mais elle **se termine proprement** :
`OnDataAvailable` est bien appelé, `DocumentLoadListener` garde la référence
jusqu'au bout, et la boucle ne se déclenche pas.

---

## Ce qu'on a écarté

Liste des hypothèses testées et **infirmées**, pour ne pas les reprendre :

| Hypothèse | Méthode de test | Résultat |
|---|---|---|
| Enregistrement du composant différent (`components.conf`, `protocol_config`) | Comparaison caractère pour caractère avec `ExchangeMessageChannel`/`ExchangeService` | **Identique** |
| Interfaces C++ manquantes sur le canal (`nsIChannel`, `nsIRequest`...) | `NS_IMPL_ISUPPORTS_INHERITED` comparé ligne à ligne | **Identique** |
| Type de contenu par défaut différent | `mContentType` comparé | **Identique (`MESSAGE_RFC822`)** |
| `aUrlListener` jamais notifié (`OnStartRunningUrl`/`OnStopRunningUrl`) | Lecture du code EWS | EWS **ignore aussi** ce paramètre — pas la cause |
| `ExchangeUrl`/`nsIMsgMailNewsUrl` non implémentée chez nous | Recherche de `new ExchangeUrl()` dans EWS | **Jamais instanciée chez EWS non plus** — classe non branchée, piste sans objet |
| Conflit entre deux téléchargements concurrents du même message | Garde-fou anti-concurrence ajouté et testé | Corrige un vrai risque de conflit, **mais pas la boucle** |
| `StreamHeaders` non implémentée | Implémentée, testée | La boucle persiste à l'identique |
| Timing/latence réseau (chargement trop lent, processus tué par un délai) | Testé sur un message **déjà en cache** (pas de round-trip réseau) | **Même échec** — pas un problème de timing réseau |
| Contenu HTML déclenchant un processus de rendu séparé | EWS testé avec de vrais e-mails HTML (M365) | EWS **gère aussi du HTML** sans boucler — pas la cause |
| Confusion entre deux canaux différents pour l'échec observé | `owner=` (pointeur du canal) ajouté aux logs `OnStartRequest`/`OnStopRequest` | **Même canal** du début à la fin — pas une confusion de canaux |
| `nsIParentRedirectingChannel`/`nsIRedirectResultListener`/`nsIAsyncVerifyRedirectCallback` non implémentées | Recherche dans le code EWS | **EWS ne les implémente pas non plus** |
| Méthode manquante sur `CarbonioMessageChannel` par rapport à `ExchangeMessageChannel` | Diff exhaustif de toutes les méthodes implémentées | **Aucune différence** |
| `FetchMimePart`/`nsIMsgMessageFetchPartService` manquante sur `CarbonioService` | Diff exhaustif + implémentation + log d'appel | Manquait réellement (lacune comblée), mais **jamais appelée pendant l'ouverture d'un message** — pas la cause |

**Ce qui reste comme piste principale, non vérifiable depuis l'extérieur** : une
différence de **timing d'exécution interne** (l'ordre exact des appels, le
moment où certains états deviennent visibles) entre notre canal et celui d'EWS,
invisible par comparaison statique de code, qui perturbe le suivi de référence
que fait `DocumentLoadListener`. Vérifier ça demanderait de déboguer
interactivement le code interne de Gecko (`netwerk/ipc/DocumentLoadListener.cpp`),
ce qu'on n'a pas encore fait.

---

## Repères utiles pour la suite

- **`nsIChannel`** : représente "un chargement de contenu en cours". `AsyncOpen()` le démarre.
- **`nsIStreamListener`** : reçoit le contenu (`OnStartRequest` → `OnDataAvailable`* → `OnStopRequest`).
- **Le store local (mbox)** : copie locale des messages sur disque. `AsyncReadMessageFromStore` (partagé avec EWS) la relit.
- **`DocumentLoadListener` / Fission** : décide, pour chaque navigation, dans quel processus afficher le contenu.
- **`NO_PARENT_CHANNEL`** : l'erreur précise isolée — vient de ce mécanisme de sélection de processus, pas de notre logique métier (authentification, récupération du message — tout ça fonctionne).

## Journal des mises à jour

- Version initiale : reconstitution du chemin complet et isolation du point de blocage.
