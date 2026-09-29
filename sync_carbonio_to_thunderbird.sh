#!/usr/bin/env bash
#
# sync_carbonio_to_thunderbird.sh — Synchronise le dépôt carbonio-thunderbird-connector
# vers un checkout local de Thunderbird (mozilla-central + comm-central).
#
# Deux mécanismes distincts, selon la nature du fichier :
#   - "new-files/"  : fichiers qui n'existent PAS dans Thunderbird -> reliés par
#                     lien symbolique. Toute édition dans le dépôt est visible
#                     immédiatement côté Thunderbird, sans jamais recopier.
#   - "patches/"    : modifications sur des fichiers qui EXISTENT DÉJÀ dans
#                     Thunderbird (ex: components.conf) -> appliquées avec `patch`,
#                     jamais en écrasant le fichier original par un lien.
#
# Configuration : voir sync.conf.example, à copier en sync.conf à côté du script.
#
# Usage :
#   ./sync_carbonio_to_thunderbird.sh link            # (re)crée les symlinks des nouveaux fichiers
#   ./sync_carbonio_to_thunderbird.sh patch            # applique les patchs non encore appliqués
#   ./sync_carbonio_to_thunderbird.sh all              # link + patch
#   ./sync_carbonio_to_thunderbird.sh status           # affiche l'état (liens en place ? patchs appliqués ?)
#   ./sync_carbonio_to_thunderbird.sh unlink           # retire les symlinks (nettoyage)
#
# Ajoute --dry-run à n'importe quelle commande pour prévisualiser sans rien modifier.
# Ajoute --config /chemin/sync.conf pour utiliser un autre fichier de config.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="$SCRIPT_DIR/sync.conf"
DRY_RUN=0
COMMAND=""

# --- Analyse des arguments -----------------------------------------------

for arg in "$@"; do
  case "$arg" in
    --dry-run)
      DRY_RUN=1
      ;;
    --config)
      : # la valeur est traitée juste après, voir boucle suivante
      ;;
    link|patch|all|status|unlink)
      COMMAND="$arg"
      ;;
  esac
done

# Gestion de --config <valeur> (nécessite de repérer l'argument suivant)
args=("$@")
for i in "${!args[@]}"; do
  if [[ "${args[$i]}" == "--config" ]]; then
    next_index=$((i + 1))
    if [[ -n "${args[$next_index]:-}" ]]; then
      CONFIG_FILE="${args[$next_index]}"
    fi
  fi
done

if [[ -z "$COMMAND" ]]; then
  echo "Erreur : commande manquante (link|patch|all|status|unlink)." >&2
  echo "Usage : $0 {link|patch|all|status|unlink} [--dry-run] [--config chemin]" >&2
  exit 1
fi

# --- Chargement de la configuration ---------------------------------------

if [[ ! -f "$CONFIG_FILE" ]]; then
  echo "Erreur : fichier de configuration introuvable : $CONFIG_FILE" >&2
  echo "Copie sync.conf.example en sync.conf et adapte REPO_DIR/COMM_DIR." >&2
  exit 1
fi

# shellcheck source=/dev/null
source "$CONFIG_FILE"

: "${REPO_DIR:?REPO_DIR non défini dans $CONFIG_FILE}"
: "${COMM_DIR:?COMM_DIR non défini dans $CONFIG_FILE}"

REPO_DIR="$(cd "$REPO_DIR" && pwd)"
COMM_DIR="$(cd "$COMM_DIR" && pwd)"

NEW_FILES_DIR="$REPO_DIR/new-files"
PATCHES_DIR="$REPO_DIR/patches"

log() { echo "[sync] $*"; }
dry() { [[ "$DRY_RUN" -eq 1 ]] && echo "[dry-run] $*" && return 0 || return 1; }

# --- Commande : link -------------------------------------------------------

do_link() {
  if [[ ! -d "$NEW_FILES_DIR" ]]; then
    log "Aucun dossier new-files/ trouvé, rien à lier."
    return 0
  fi

  local count=0
  while IFS= read -r -d '' source_file; do
    local relative_path="${source_file#"$NEW_FILES_DIR"/}"
    local target_path="$COMM_DIR/$relative_path"
    local target_dir
    target_dir="$(dirname "$target_path")"

    if dry "mkdir -p '$target_dir' && ln -sf '$source_file' '$target_path'"; then
      continue
    fi

    mkdir -p "$target_dir"

    if [[ -e "$target_path" && ! -L "$target_path" ]]; then
      log "ATTENTION : $target_path existe déjà et n'est pas un lien symbolique. Ignoré (pour ne pas écraser un vrai fichier Mozilla)."
      continue
    fi

    ln -sf "$source_file" "$target_path"
    log "Lié : $relative_path"
    count=$((count + 1))
  done < <(find "$NEW_FILES_DIR" -type f -print0)

  if [[ "$DRY_RUN" -eq 0 ]]; then log "$count fichier(s) lié(s)."; fi
}

# --- Commande : unlink ------------------------------------------------------

do_unlink() {
  if [[ ! -d "$NEW_FILES_DIR" ]]; then
    log "Aucun dossier new-files/ trouvé, rien à délier."
    return 0
  fi

  local count=0
  while IFS= read -r -d '' source_file; do
    local relative_path="${source_file#"$NEW_FILES_DIR"/}"
    local target_path="$COMM_DIR/$relative_path"

    if [[ -L "$target_path" ]]; then
      if dry "rm '$target_path'"; then
        continue
      fi
      rm "$target_path"
      log "Délié : $relative_path"
      count=$((count + 1))
    fi
  done < <(find "$NEW_FILES_DIR" -type f -print0)

  if [[ "$DRY_RUN" -eq 0 ]]; then log "$count lien(s) retiré(s)."; fi
}

# --- Commande : patch -------------------------------------------------------

do_patch() {
  if [[ ! -d "$PATCHES_DIR" ]]; then
    log "Aucun dossier patches/ trouvé, rien à appliquer."
    return 0
  fi

  shopt -s nullglob
  local patch_files=("$PATCHES_DIR"/*.patch)
  shopt -u nullglob

  if [[ ${#patch_files[@]} -eq 0 ]]; then
    log "Aucun fichier .patch trouvé dans $PATCHES_DIR."
    return 0
  fi

  for patch_file in "${patch_files[@]}"; do
    local name
    name="$(basename "$patch_file")"

    # --forward (-N) fait que patch détecte lui-même un patch déjà appliqué,
    # mais retourne tout de même un code de sortie non-nul dans ce cas (avec
    # le message "Skipping patch" / "previously applied") — on vérifie donc le
    # message en priorité, pas seulement le code de sortie.
    local rc=0
    patch -p1 -d "$COMM_DIR" --dry-run --forward --silent < "$patch_file" > /tmp/patch_check.$$ 2>&1 || rc=$?

    if grep -q "previously applied\|Skipping patch" /tmp/patch_check.$$; then
      log "Déjà appliqué, ignoré : $name"
      rm -f /tmp/patch_check.$$
      continue
    elif [[ $rc -ne 0 ]]; then
      log "ÉCHEC (dry-run) : $name — probablement en conflit avec l'état actuel du fichier. Patch NON appliqué."
      log "  Inspecte manuellement : patch -p1 -d '$COMM_DIR' --dry-run --forward < '$patch_file'"
      rm -f /tmp/patch_check.$$
      continue
    fi
    rm -f /tmp/patch_check.$$

    if dry "patch -p1 -d '$COMM_DIR' --forward --batch < '$patch_file'"; then
      continue
    fi

    patch -p1 -d "$COMM_DIR" --forward --batch < "$patch_file"
    log "Appliqué : $name"
  done
}

# --- Commande : status -------------------------------------------------------

do_status() {
  log "Dépôt   : $REPO_DIR"
  log "Cible   : $COMM_DIR"
  echo

  if [[ -d "$NEW_FILES_DIR" ]]; then
    log "-- Nouveaux fichiers --"
    while IFS= read -r -d '' source_file; do
      local relative_path="${source_file#"$NEW_FILES_DIR"/}"
      local target_path="$COMM_DIR/$relative_path"
      if [[ -L "$target_path" ]]; then
        echo "  [lié]      $relative_path"
      elif [[ -e "$target_path" ]]; then
        echo "  [CONFLIT]  $relative_path (existe déjà, pas un lien)"
      else
        echo "  [absent]   $relative_path"
      fi
    done < <(find "$NEW_FILES_DIR" -type f -print0)
    echo
  fi

  if [[ -d "$PATCHES_DIR" ]]; then
    log "-- Patchs --"
    shopt -s nullglob
    for patch_file in "$PATCHES_DIR"/*.patch; do
      local name
      name="$(basename "$patch_file")"
      local rc=0
      patch -p1 -d "$COMM_DIR" --dry-run --forward --silent < "$patch_file" > /tmp/patch_status.$$ 2>&1 || rc=$?

      if grep -q "previously applied\|Skipping patch" /tmp/patch_status.$$; then
        echo "  [appliqué]     $name"
      elif [[ $rc -eq 0 ]]; then
        echo "  [en attente]   $name"
      else
        echo "  [CONFLIT]      $name (ni appliqué proprement, ni applicable tel quel)"
      fi
      rm -f /tmp/patch_status.$$
    done
    shopt -u nullglob
  fi
}

# --- Dispatch ----------------------------------------------------------------

case "$COMMAND" in
  link) do_link ;;
  unlink) do_unlink ;;
  patch) do_patch ;;
  all) do_link && do_patch ;;
  status) do_status ;;
esac
