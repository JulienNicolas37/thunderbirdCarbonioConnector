# Procédure — cloner et builder Thunderbird depuis zéro (checkout propre)

**Contexte** : rédigée après une resynchro complète depuis une base propre (`~/Developement/thunderbird-clean/`), pour disposer d'un socle de comparaison sans aucune modification manuelle accumulée — utile chaque fois qu'un doute apparaît sur l'état d'un checkout de travail existant.

**Emplacement de référence dans cette session** : `~/Developement/thunderbird/source/` (checkout de travail principal, avec tous nos patches) vs `~/Developement/thunderbird-clean/source/` (checkout propre, pour comparaison). Adapter les chemins selon le besoin du moment.

---

## 1. Cloner les sources

Deux dépôts Mercurial distincts et imbriqués : `mozilla-central` (le cœur, Firefox) et `comm-central` (Thunderbird proprement dit), ce dernier **à l'intérieur** du premier, dans un dossier nommé littéralement `comm`.

```bash
mkdir -p ~/Developement/thunderbird-clean
cd ~/Developement/thunderbird-clean

hg clone https://hg.mozilla.org/mozilla-central source
cd source
hg clone https://hg.mozilla.org/comm-central comm
```

**Le clone de `comm` est long** (dépôt conséquent) et peut sembler se terminer alors qu'il a juste été oublié ou interrompu. **Toujours vérifier explicitement qu'il a bien abouti avant de continuer** :

```bash
ls ~/Developement/thunderbird-clean/source/comm/ | head -10
```

Si la commande renvoie *"Aucun fichier ou répertoire de ce type"*, ou si le dossier existe mais semble vide/incomplet, relancer simplement le clone (il n'écrasera rien s'il a déjà partiellement récupéré des données, Mercurial reprend proprement) :

```bash
cd ~/Developement/thunderbird-clean/source
hg clone https://hg.mozilla.org/comm-central comm
```

## 2. Créer le fichier `mozconfig`

À la racine de `source/` (pas dans `comm/`). **La ligne suivante est indispensable** — sans elle, le build produit Firefox au lieu de Thunderbird, sans erreur explicite avant l'étape de build elle-même :

```
ac_add_options --enable-project=comm/mail
```

Si un `mozconfig` existant (d'un autre checkout) doit servir de référence, le copier **puis vérifier que cette ligne y est bien présente** après coup — `./mach bootstrap` peut réécrire/compléter ce fichier automatiquement et il est facile de perdre une ligne copiée manuellement en amont dans la confusion. Vérification rapide :

```bash
grep -n "enable-project=comm/mail" ~/Developement/thunderbird-clean/source/mozconfig
```

(doit afficher la ligne ; si rien ne s'affiche, l'ajouter avec `echo "ac_add_options --enable-project=comm/mail" >> mozconfig`)

## 3. Lancer le bootstrap

```bash
cd ~/Developement/thunderbird-clean/source
./mach bootstrap
```

**Questions posées, et réponses à donner** (validées dans cette session) :

| Question | Réponse | Pourquoi |
|---|---|---|
| *"Please choose the version of Firefox you want to build"* | **`2` — Firefox for Desktop** (pas `1`, Artifact Mode) | L'Artifact Mode télécharge des binaires précompilés et ne permet pas de recompiler du C++/Rust modifié localement — indispensable pour tout travail sur un module comme Carbonio. |
| *"Will you be using agentic coding tools to work on Firefox?"* | **`N`** (non) | Concerne l'intégration d'outils IA travaillant directement dans le dépôt via un accès fichier local — sans rapport avec un travail mené par échange en chat. |
| *"Would you like to run a configuration wizard to ensure Mercurial is optimally configured?"* | **`Y`** (oui, par défaut) | Optimisations/extensions Mercurial standard recommandées par Mozilla, sans impact sur le code produit. |
| *"Would you like to enable sccache?"* | **`Y`** (oui, par défaut) | Cache de compilation qui accélère les rebuilds ultérieurs, sans impact sur le résultat du build. |

Le bootstrap ajoute automatiquement `ac_add_options --with-ccache=sccache` au `mozconfig` — c'est normal, à laisser tel quel.

## 4. Premier build

```bash
./mach build
```

**Peut échouer avec `ERROR: Cannot find project comm/mail`** — dans ce cas, revenir à l'étape 2 : soit le dossier `comm/` n'existe pas/est incomplet (revérifier l'étape 1), soit la ligne `--enable-project=comm/mail` manque du `mozconfig` (revérifier l'étape 2).

Une fois le build terminé sans erreur, lancer avec un profil de test dédié pour confirmer que c'est bien **Thunderbird** qui démarre (pas Firefox) :

```bash
mkdir -p ~/nom-du-profil-test
./mach run --profile ~/nom-du-profil-test
```

## Rappels utiles, valables aussi bien sur ce checkout propre que sur un checkout de travail

- `mailnews/protocols/moz.build` et `rust/gkrust/src/lib.rs` sont les deux points d'enregistrement nécessaires pour qu'un nouveau module protocole (type Carbonio) soit seulement *compilé/lié* — voir `patches/0001-*`/`0002-*` du projet Carbonio.
- Sandbox : si `CanCreateUserNamespace() unshare(CLONE_NEWPID): EPERM` apparaît au lancement, ajouter `MOZ_DISABLE_CONTENT_SANDBOX=1` devant la commande `./mach run`.
- `comm/` est un **dépôt Mercurial imbriqué séparé** — toujours se placer explicitement dedans (`cd .../source/comm`) avant un `hg diff`/`hg add` visant un fichier de ce sous-dossier, sinon Mercurial refuse avec *"path is inside nested repo 'comm'"*.
