# Mécanique d'ouverture d'un message — du double-clic à l'affichage (RÉSOLU)

Ce document retrace, étape par étape, tout ce qui se passe quand on double-clique
sur un message dans la liste, et documente précisément où notre connecteur bloque
aujourd'hui. Il sert de base de travail interne, et de matière première pour un
futur échange avec la communauté Thunderbird/Gecko.

**Statut : RÉSOLU.** Voir la section [La résolution](#la-résolution) pour le
correctif final. Les sections qui suivent, en particulier
[Où ça bloque](#où-ça-bloque) et [Ce qu'on a écarté](#ce-quon-a-écarté),
retracent le cheminement complet de l'investigation et restent une référence
utile pour comprendre la mécanique — mais le correctif lui-même est dans la
dernière section.

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

**Cause identifiée avec certitude, via résolution de pile d'appel complète
(`MozWalkTheStack` + `addr2line` sur le binaire de debug local) :**

```
mozilla::net::DocumentLoadListener::TriggerRedirectToRealChannel(...)
  → vérifie un Maybe<...>::isSome() → false
  → CarbonioMessageChannel::Cancel(NS_ERROR_DOCUMENT_LOAD_LISTENER_NO_PARENT_CHANNEL)
  → mReadRequest->Cancel(...) (notre nsInputStreamPump)
  → OfflineMessageReadListener::OnStopRequest (échec)
  → notre SpyStreamListener::OnStopRequest (échec, aucune donnée jamais livrée)
```

C'est bien `DocumentLoadListener::TriggerRedirectToRealChannel` — la fonction qui
remplace le canal placeholder par le "vrai" canal — qui **annule elle-même
notre canal**, parce qu'une vérification interne (`Maybe<T>::isSome()`, juste
avant l'appel à `Cancel`) échoue : une référence qu'elle attend de trouver
encore valide à ce stade est absente. C'est précisément l'origine du nom
`NO_PARENT_CHANNEL`.

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

**Ce qu'il reste à comprendre** : *pourquoi* cette référence est absente
spécifiquement pour notre canal, alors que la structure du code (interfaces,
enregistrement, `LoadInfo`, type de contenu...) est identique à EWS sur tous
les points vérifiables statiquement. La réponse est probablement dans le code
source exact de `TriggerRedirectToRealChannel`
(`netwerk/ipc/DocumentLoadListener.cpp`), qu'on n'a pas pu consulter en détail
depuis l'extérieur — c'est le point précis à poser à la communauté.

### Confirmation croisée côté EWS (même instrumentation, cas qui réussit)

En posant exactement les mêmes sondes (`Cancel`, pile d'appel) sur
`ExchangeMessageChannel`, on obtient une comparaison directe :

- **`Cancel` n'est jamais appelé côté EWS**, dans aucun cycle observé.
- `OnDataAvailable` (avec de vraies données, ex. `count=95154`) puis
  `OnStopRequest status=00000000` (succès) s'enchaînent proprement à chaque
  fois.
- **Tous les `AsyncOpen` d'EWS partagent le même PID que `LoadMessage`** —
  aucun processus enfant n'est jamais créé.

Chez Carbonio, à l'inverse, un **second PID (processus enfant) apparaît
systématiquement** à chaque cycle, en plus de l'`AsyncOpen` dans le processus
parent.

**Ça resserre la question à un point précis** : `TriggerRedirectToRealChannel`
ne semble poser problème que lorsqu'un changement de processus est réellement
nécessaire. EWS n'en a jamais besoin (tout reste dans le processus parent) ;
Carbonio, pour une raison qui reste à déterminer, en déclenche un à chaque
fois — et c'est précisément à cette étape de redirection vers l'autre
processus que la référence attendue se révèle absente.

**La question resserrée pour la communauté** : qu'est-ce qui, dans la
sélection du processus de destination pour un chargement `docShell.LoadURI()`
(type de contenu détecté, `LoadInfo`, politique de sécurité, ou autre),
pourrait faire qu'un schéma d'URI personnalisé comme `x-moz-carbonio`
déclenche un changement de processus alors qu'un schéma structurellement
identique (`x-moz-ews`) n'en déclenche jamais ?

---

### Le mécanisme complet, lu directement dans le code source local

En lisant `netwerk/ipc/DocumentLoadListener.cpp` (présent en entier dans le
checkout mozilla-central local — pas besoin de deviner depuis des extraits
web), on trouve la chaîne complète, avec les noms exacts.

**1. `RedirectToRealChannel`** enregistre notre canal dans un registre
partagé (`RedirectChannelRegistrar`), sous un identifiant généré :

```cpp
mRedirectChannelId = nsContentUtils::GenerateLoadIdentifier();
MOZ_ALWAYS_SUCCEEDS(registrar->RegisterChannel(chan, mRedirectChannelId,
                                               ownerContentParentId));

if (aDestinationProcess) {
  // Chemin "changement de processus" : IPC vers le processus de contenu
  // (SendCrossProcessRedirect), puis retour via FinishReplacementChannelSetup
  ...
} else {
  // Chemin "même processus" : résout directement la promesse d'ouverture,
  // ne repasse JAMAIS par la recherche ci-dessous.
  mOpenPromise->Resolve(...);
  ...
}
```

**2. `FinishReplacementChannelSetup`** (appelé uniquement sur le chemin
"changement de processus") recherche, sous ce même identifiant, un objet
**différent** — un `nsIParentChannel` (pas notre `nsIChannel` directement,
une enveloppe séparée) :

```cpp
nsCOMPtr<nsIParentChannel> redirectChannel;
nsresult rv = registrar->GetParentChannel(mRedirectChannelId,
                                          getter_AddRefs(redirectChannel));
if (NS_FAILED(rv) || !redirectChannel) {
  aResult = NS_ERROR_DOCUMENT_LOAD_LISTENER_NO_PARENT_CHANNEL;
}
...
if (NS_FAILED(aResult)) {
  ...
  mChannel->Cancel(aResult);   // ← exactement ce qu'on observe en logs
  mChannel->Resume();
  return;
}
```

**Le point clé** : ce `nsIParentChannel` est une enveloppe **séparée** de
notre canal (voir `ParentChannelWrapper : public nsIParentChannel` dans
`netwerk/ipc/ParentChannelWrapper.h`), normalement créée et enregistrée par
le code générique de navigation web une fois le processus de destination
confirmé. **Si rien ne crée jamais cette enveloppe pour notre cas — parce que
ce mécanisme cible la navigation web classique (onglets, iframes), pas un
message chargé via `nsIMsgMessageService` dans un docShell intégré — la
recherche revient bredouille, et notre canal est annulé.**

Ça explique aussi, précisément, pourquoi EWS n'est jamais concerné : sans
changement de processus (`aDestinationProcess` vide), le code **saute
directement** à la résolution de la promesse d'ouverture — il ne passe jamais
par cette recherche de `nsIParentChannel`.

**La question ultime, désormais totalement précise** : qu'est-ce qui décide,
avant tout ça, qu'un chargement Carbonio nécessite un changement de processus
alors qu'un chargement EWS structurellement identique n'en a jamais besoin ?
C'est cette décision-là qui a permis de trouver le vrai correctif — voir
[La résolution](#la-résolution) ci-dessous.

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
| Confusion sur l'origine réelle de l'échec (canal enfant vs canal principal, `DocumentLoadListener` vs autre) | Pile d'appel complète capturée (`MozWalkTheStack`) et résolue (`addr2line`) aux points clés (`Cancel`, `OnStopRequest`) | **Résolu avec certitude** : c'est `DocumentLoadListener::TriggerRedirectToRealChannel` qui annule notre canal — voir [Où ça bloque](#où-ça-bloque) |

**Ce qui a permis de trancher, finalement** : la comparaison croisée avec EWS
en conditions réelles (même instrumentation, même code de sondage, un clic
chacun) a montré qu'EWS ne change **jamais** de processus, alors que Carbonio
en change **systématiquement**. Cette différence de comportement — pas
visible en comparant seulement le code statique de nos deux connecteurs — a
mené directement à la vraie cause. Voir [La résolution](#la-résolution).

---

## La résolution

Deux fonctions Gecko maintiennent chacune une **liste blanche de schémas
d'URI** codée en dur, énumérant les protocoles mail que Thunderbird a le
droit de garder dans le processus parent (`imap`, `mailbox`, `news`, `nntp`,
`snews`, `x-moz-ews`, `x-moz-graph`). Notre schéma, `x-moz-carbonio`, n'y
figurait dans aucune des deux — pas par bug, simplement parce que c'est un
module tiers que les mainteneurs de Thunderbird ne pouvaient pas connaître.

### La vraie décision : `IsolationBehaviorForURI`

**`dom/ipc/ProcessIsolation.cpp`**, fonction `IsolationBehaviorForURI` :

```cpp
// Protocols used by Thunderbird to display email messages.
if (scheme == "imap"_ns || scheme == "mailbox"_ns || scheme == "news"_ns ||
    scheme == "nntp"_ns || scheme == "snews"_ns || scheme == "x-moz-ews"_ns ||
    scheme == "x-moz-graph"_ns) {
  return IsolationBehavior::Parent;
}
// ... (pas de correspondance pour nous)
return IsolationBehavior::WebContent;   // ← notre schéma tombait ici
```

C'est cette fonction qui calcule `IsolationBehavior`, dont dérive
`options.mRemoteType` dans `IsolationOptionsForNavigation`, comparé ensuite à
`currentRemoteType` dans `DocumentLoadListener::MaybeTriggerProcessSwitch` :

```cpp
if (currentRemoteType == options.mRemoteType && ...) {
  return false;   // pas de changement de processus — le cas d'EWS
}
```

Faute de correspondance, Carbonio tombait sur `IsolationBehavior::WebContent`
→ `mRemoteType` différent du type courant → `MaybeTriggerProcessSwitch`
déclenche un vrai changement de processus à chaque chargement de message →
`TriggerRedirectToRealChannel` cherche le `nsIParentChannel` enregistré pour
ce changement (voir plus haut) → jamais créé pour ce genre de chargement →
`Cancel(NO_PARENT_CHANNEL)` → boucle infinie côté UI.

**Le correctif** : ajouter `x-moz-carbonio` à cette liste.

```diff
--- a/dom/ipc/ProcessIsolation.cpp
+++ b/dom/ipc/ProcessIsolation.cpp
@@ -354,7 +354,7 @@ static IsolationBehavior IsolationBehavi
   // Protocols used by Thunderbird to display email messages.
   if (scheme == "imap"_ns || scheme == "mailbox"_ns || scheme == "news"_ns ||
       scheme == "nntp"_ns || scheme == "snews"_ns || scheme == "x-moz-ews"_ns ||
-      scheme == "x-moz-graph"_ns) {
+      scheme == "x-moz-graph"_ns || scheme == "x-moz-carbonio"_ns) {
     return IsolationBehavior::Parent;
   }
```

Patch complet : `patches/allow-x-moz-carbonio-in-process-isolation.patch`.
**C'est le correctif décisif** — celui qui a réellement arrêté la boucle et
permis l'affichage du message.

### Une seconde liste, apparentée mais insuffisante seule

**`docshell/base/nsDocShell.cpp`**, fonction `CanLoadInParentProcess`, contient
une liste quasi identique, utilisée cette fois pour le sens inverse (un
chargement qui revient du processus de contenu vers le parent). On l'a
corrigée aussi par cohérence (patch
`patches/allow-x-moz-carbonio-in-parent-process.patch`), mais **ce patch seul
n'a pas suffi à arrêter la boucle** — la correction déterminante est bien
celle de `ProcessIsolation.cpp` ci-dessus. Les deux patches sont conservés
côte à côte car ils couvrent des chemins de code différents et légitimes.

### Validation

Avec les deux patches appliqués, un clic sur un message donne, en logs :
`LoadMessage` appelé **une seule fois**, `AsyncOpen` reste dans le **même
PID**, `SpyStreamListener::OnDataAvailable` se déclenche enfin (jamais observé
auparavant côté Carbonio), `OnStopRequest status=00000000`, et surtout — le
contenu du message s'affiche correctement dans le volet de lecture.

### Pour une contribution upstream

Ces deux patches ajoutent notre schéma à des listes qui contiennent déjà
tous les protocoles mail internes de Thunderbird (EWS, IMAP, NNTP...) — le
genre d'ajout qu'un mainteneur Thunderbird accepterait sans discussion, à
la manière de ce qui a déjà été fait pour `x-moz-ews` et `x-moz-graph`.

---



- **`nsIChannel`** : représente "un chargement de contenu en cours". `AsyncOpen()` le démarre.
- **`nsIStreamListener`** : reçoit le contenu (`OnStartRequest` → `OnDataAvailable`* → `OnStopRequest`).
- **Le store local (mbox)** : copie locale des messages sur disque. `AsyncReadMessageFromStore` (partagé avec EWS) la relit.
- **`DocumentLoadListener` / Fission** : décide, pour chaque navigation, dans quel processus afficher le contenu.
- **`NO_PARENT_CHANNEL`** : l'erreur précise isolée — vient de ce mécanisme de sélection de processus, pas de notre logique métier (authentification, récupération du message — tout ça fonctionne).

## Journal des mises à jour

- Version initiale : reconstitution du chemin complet et isolation du point de blocage.
- Mise à jour : cause identifiée avec certitude via résolution de pile d'appel (`DocumentLoadListener::TriggerRedirectToRealChannel` annule notre canal) ; ajout des hypothèses `FetchMimePart` et "confusion d'origine de l'échec", toutes deux écartées/résolues.
- Mise à jour : confirmation croisée côté EWS avec la même instrumentation — EWS ne change jamais de processus et `Cancel` n'y est jamais appelé, resserrant la question à "pourquoi Carbonio déclenche-t-il un changement de processus, contrairement à EWS ?".
- Mise à jour : mécanisme complet lu directement dans `netwerk/ipc/DocumentLoadListener.cpp` (checkout local) — le `nsIParentChannel` attendu par `FinishReplacementChannelSetup` n'est jamais enregistré pour notre canal, uniquement sur le chemin "changement de processus" (jamais emprunté par EWS).
- **RÉSOLU** : cause racine identifiée jusqu'au bout dans `dom/ipc/ProcessIsolation.cpp` (`IsolationBehaviorForURI`) — liste blanche de schémas mail codée en dur, `x-moz-carbonio` absent. Correctif appliqué et validé en conditions réelles (patches dans `patches/allow-x-moz-carbonio-in-process-isolation.patch` et `patches/allow-x-moz-carbonio-in-parent-process.patch`) : le message s'affiche enfin correctement.
