# thunderbirdCarbonioConnector

Connecteur natif Carbonio pour Thunderbird — développé en fork de comm-central,
sur le modèle du module Exchange/EWS existant.

## Structure du dépôt

- **`docs/`** — documents de conception et scripts d'exploration produits en amont :
  - `carbonio-thunderbird-connecteur-conception-phase1.md` — architecture générale,
    mapping des fichiers à créer (calqué sur le module EWS).
  - `carbonio-thunderbird-connecteur-phase1-specification.md` — spécification
    détaillée du contrat de synchro de dossiers, validée empiriquement contre
    une instance Carbonio réelle.
  - `carbonio_explorer.py` — outil d'exploration de l'API SOAP/JSON Carbonio
    (auth, dossiers, sync, messages), utilisé pour produire la spec ci-dessus.
  - `config.ini.example` — exemple de config pour `carbonio_explorer.py`.

- **`new-files/`** — fichiers du connecteur qui n'existent pas dans comm-central
  (nouveau code C++ et Rust). Reliés par **lien symbolique** dans le checkout
  Thunderbird via `sync_carbonio_to_thunderbird.sh` — ne jamais copier ces
  fichiers à la main, le script s'en charge.

- **`patches/`** — modifications sur des fichiers qui existent déjà dans
  comm-central (ex: `components.conf`, futures modifs de `protocol_shared`
  pour l'authentification). Appliqués via le même script, avec `patch`.

- **`sync_carbonio_to_thunderbird.sh`** + **`sync.conf.example`** — script de
  synchronisation entre ce dépôt et un checkout Mercurial local de Thunderbird.
  Voir l'en-tête du script pour l'usage détaillé (`status`, `link`, `patch`,
  `all`, `unlink`, avec `--dry-run` disponible partout).

## Statut actuel

Phase 1 (compte en lecture seule : auth, synchro de hiérarchie de dossiers,
récupération des messages) — conception et spécification validées, scaffold de
code pas encore commencé.
