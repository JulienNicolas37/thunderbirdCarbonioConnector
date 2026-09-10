#!/usr/bin/env python3
"""
carbonio_explorer.py — Petit outil d'exploration de l'API SOAP/JSON Carbonio.

Objectif : comprendre concrètement le comportement réel de l'API (forme des
réponses, transport du token, delta de synchronisation) avant de concevoir le
connecteur natif Thunderbird. Chaque requête et chaque réponse est journalisée
en détail pour analyse ultérieure.

Configuration :
    Les informations de connexion (host, user, password, insecure, log_file)
    peuvent être renseignées dans un fichier de configuration INI plutôt que
    sur la ligne de commande. Par défaut le script cherche "config.ini" à
    côté de lui ; utilise --config pour pointer vers un autre fichier.

    Exemple de config.ini :

        [carbonio]
        host = srv.example.com
        user = user@example.com
        password = SuperSecretPassword
        insecure = true
        log_file = carbonio_explorer.log

    Tout argument passé en ligne de commande prend le pas sur la valeur du
    fichier de configuration.

Usage :
    python3 carbonio_explorer.py auth
    python3 carbonio_explorer.py folders
    python3 carbonio_explorer.py sync
    python3 carbonio_explorer.py sync --sync-token "TOKEN_PRECEDENT"
    python3 carbonio_explorer.py message --id 257
    python3 carbonio_explorer.py search --query 'in:inbox'
    python3 carbonio_explorer.py search --query 'in:inbox' --limit 5 --offset 25
    python3 carbonio_explorer.py --config /chemin/autre_config.ini auth
    python3 carbonio_explorer.py --host srv.example.com --user u --password p auth  # sans config
"""

import argparse
import configparser
import json
import logging
import os
import sys

import requests
import urllib3

logger = logging.getLogger("carbonio_explorer")

CONFIG_SECTION = "carbonio"
DEFAULT_CONFIG_PATH = "config.ini"


def setup_logging(log_file: str | None) -> None:
    handlers = [logging.StreamHandler(sys.stdout)]
    if log_file:
        handlers.append(logging.FileHandler(log_file, encoding="utf-8"))

    logging.basicConfig(
        level=logging.DEBUG,
        format="%(asctime)s [%(levelname)s] %(message)s",
        handlers=handlers,
        force=True,
    )


def load_config(config_path: str) -> dict:
    """
    Charge le fichier de configuration INI s'il existe. Retourne un dict vide
    (sans erreur) si le fichier est absent, pour permettre un usage 100% CLI.
    """
    if not os.path.isfile(config_path):
        return {}

    # interpolation=None : un mot de passe peut contenir un '%', que
    # l'interpolation par défaut de configparser essaierait d'interpréter.
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(config_path, encoding="utf-8")

    if CONFIG_SECTION not in parser:
        logger.warning(
            "Le fichier de configuration %s ne contient pas de section [%s], ignoré.",
            config_path,
            CONFIG_SECTION,
        )
        return {}

    section = parser[CONFIG_SECTION]
    config: dict = {
        "host": section.get("host", fallback=None),
        "user": section.get("user", fallback=None),
        "password": section.get("password", fallback=None),
        "log_file": section.get("log_file", fallback=None),
    }
    # getboolean gère "true/false", "yes/no", "1/0"...
    if "insecure" in section:
        config["insecure"] = section.getboolean("insecure")

    return config


def resolve_setting(cli_value, config: dict, key: str, *, required: bool = False):
    """CLI > fichier de config. Erreur claire si une valeur requise manque des deux côtés."""
    if cli_value is not None:
        return cli_value

    value = config.get(key)
    if required and value is None:
        sys.exit(
            f"Erreur : '{key}' n'est fourni ni en argument (--{key.replace('_', '-')}) "
            f"ni dans le fichier de configuration (section [{CONFIG_SECTION}])."
        )
    return value


def redact(body: dict) -> dict:
    """Retourne une copie de l'enveloppe avec le mot de passe masqué, pour le log."""
    redacted = json.loads(json.dumps(body))  # deep copy simple
    try:
        redacted["Body"]["AuthRequest"]["password"] = "***REDACTED***"
    except (KeyError, TypeError):
        pass
    return redacted


def soap_request(
    host: str,
    body: dict,
    *,
    token: str | None = None,
    verify: bool = True,
) -> dict:
    """
    Construit l'enveloppe SOAP-JSON complète, envoie la requête, journalise et
    retourne la réponse décodée.
    """
    url = f"https://{host}/service/soap"

    context: dict = {"_jsns": "urn:zimbra"}
    if token:
        context["authToken"] = token

    envelope = {
        "Header": {"context": context},
        "Body": body,
        "_jsns": "urn:zimbraSoap",
    }

    logger.info("--> POST %s", url)
    logger.debug("--> Enveloppe envoyée :\n%s", json.dumps(redact(envelope), indent=2, ensure_ascii=False))

    response = requests.post(
        url,
        json=envelope,
        headers={"Content-Type": "application/json"},
        verify=verify,
        timeout=30,
    )

    logger.info("<-- HTTP %s", response.status_code)

    try:
        decoded = response.json()
    except ValueError:
        logger.error("<-- Réponse non-JSON, contenu brut :\n%s", response.text)
        response.raise_for_status()
        raise

    logger.debug("<-- Corps de la réponse :\n%s", json.dumps(decoded, indent=2, ensure_ascii=False))

    # Carbonio renvoie les erreurs applicatives avec un code HTTP 200 mais un
    # Body.Fault — on le détecte explicitement pour ne pas le rater dans les logs.
    fault = decoded.get("Body", {}).get("Fault")
    if fault:
        logger.error("<-- Fault applicatif Carbonio :\n%s", json.dumps(fault, indent=2, ensure_ascii=False))

    response.raise_for_status()
    return decoded


def authenticate(host: str, user: str, password: str, *, verify: bool = True) -> tuple[str, int]:
    """Effectue l'AuthRequest et retourne (authToken, lifetime_ms)."""
    body = {
        "AuthRequest": {
            "_jsns": "urn:zimbraAccount",
            "csrfTokenSecured": True,
            "persistAuthTokenCookie": True,
            "generateDeviceId": True,
            "account": {"by": "name", "_content": user},
            "password": password,
        }
    }

    decoded = soap_request(host, body, verify=verify)
    auth_response = decoded["Body"]["AuthResponse"]

    token = auth_response["authToken"][0]["_content"]
    lifetime = auth_response.get("lifetime")

    logger.info("Authentification réussie. Durée de vie du token : %s ms", lifetime)
    return token, lifetime


def get_folders(host: str, token: str, *, verify: bool = True, folder_id: str = "1") -> dict:
    """Récupère l'arbre de dossiers via GetFolderRequest."""
    body = {
        "GetFolderRequest": {
            "_jsns": "urn:zimbraMail",
            "folder": {"id": folder_id},
        }
    }
    return soap_request(host, body, token=token, verify=verify)


def sync_folders(
    host: str,
    token: str,
    *,
    verify: bool = True,
    sync_token: str | None = None,
    typed: bool = False,
) -> dict:
    """
    Effectue un SyncRequest. Sans sync_token : sync initial complet.
    Avec sync_token : ne devrait renvoyer que le delta depuis ce token.

    typed=True ajoute l'attribut "typed": 1, qui d'après la doc Carbonio
    décompose le tableau "deleted" par type d'objet (dossier, message, tag...)
    plutôt que de renvoyer une liste d'IDs mélangés.
    """
    request_body: dict = {"_jsns": "urn:zimbraMail"}
    if sync_token:
        request_body["token"] = sync_token
    if typed:
        request_body["typed"] = 1

    body = {"SyncRequest": request_body}
    decoded = soap_request(host, body, token=token, verify=verify)

    new_token = decoded.get("Body", {}).get("SyncResponse", {}).get("token")
    if new_token:
        logger.info("Nouveau sync-token reçu : %s (à réutiliser pour le prochain appel sync)", new_token)

    return decoded


def get_message(host: str, token: str, msg_id: str, *, verify: bool = True, raw: bool = False) -> dict:
    """
    Récupère un message via GetMsgRequest.

    raw=True ajoute l'attribut "raw": 1, qui d'après l'API Zimbra/Carbonio
    historique bascule la réponse vers le contenu MIME brut (RFC822) plutôt
    que la représentation structurée/parsée (sujet, participants, parties MIME
    déjà décodées) obtenue par défaut.
    """
    m_spec = {"id": msg_id}
    if raw:
        m_spec["raw"] = 1

    body = {
        "GetMsgRequest": {
            "_jsns": "urn:zimbraMail",
            "m": m_spec,
        }
    }
    return soap_request(host, body, token=token, verify=verify)


def search_messages(
    host: str,
    token: str,
    *,
    verify: bool = True,
    query: str = "in:inbox",
    limit: int = 25,
    offset: int = 0,
    sort_by: str = "dateDesc",
) -> dict:
    """
    Recherche des messages via SearchRequest — l'appel Zimbra/Carbonio dédié
    à la liste de messages (sujet, expéditeur, date, drapeaux...), par
    opposition à SyncRequest qui ne donne que des IDs bruts sans métadonnées.

    query : syntaxe de recherche Zimbra, ex. 'in:inbox', 'in:"Inbox/Test 01"'.
    """
    body = {
        "SearchRequest": {
            "_jsns": "urn:zimbraMail",
            "types": "message",
            "query": query,
            "limit": limit,
            "offset": offset,
            "sortBy": sort_by,
        }
    }
    return soap_request(host, body, token=token, verify=verify)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "--config",
        default=DEFAULT_CONFIG_PATH,
        help=f"Chemin du fichier de configuration INI (défaut: {DEFAULT_CONFIG_PATH})",
    )
    parser.add_argument("--host", default=None, help="Nom d'hôte du serveur Carbonio (sans https://)")
    parser.add_argument("--user", default=None, help="Adresse email du compte")
    parser.add_argument("--password", default=None, help="Mot de passe du compte")
    parser.add_argument(
        "--insecure",
        action="store_true",
        default=None,
        help="Désactive la vérification du certificat TLS (sinon, valeur du fichier de config ou False)",
    )
    parser.add_argument("--log-file", default=None, help="Fichier dans lequel écrire les logs en plus de la console")

    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("auth", help="Authentifie et affiche le token obtenu")

    folders_parser = subparsers.add_parser("folders", help="Récupère l'arbre de dossiers")
    folders_parser.add_argument("--folder-id", default="1", help="ID du dossier racine à explorer (défaut: 1)")

    sync_parser = subparsers.add_parser("sync", help="Effectue un SyncRequest (complet ou delta)")
    sync_parser.add_argument("--sync-token", default=None, help="Sync-token précédent, pour observer le delta")
    sync_parser.add_argument(
        "--typed",
        action="store_true",
        help="Ajoute typed=1 : décompose 'deleted' par type d'objet plutôt qu'une liste d'IDs mélangés",
    )

    message_parser = subparsers.add_parser("message", help="Récupère le contenu d'un message")
    message_parser.add_argument("--id", required=True, help="ID du message à récupérer")
    message_parser.add_argument(
        "--raw",
        action="store_true",
        help="Ajoute raw=1 : demande le contenu MIME brut (RFC822) plutôt que la forme structurée/parsée",
    )

    search_parser = subparsers.add_parser("search", help="Recherche des messages (liste + métadonnées)")
    search_parser.add_argument("--query", default="in:inbox", help="Requête de recherche Zimbra (défaut: in:inbox)")
    search_parser.add_argument("--limit", type=int, default=25, help="Nombre max de résultats (défaut: 25)")
    search_parser.add_argument("--offset", type=int, default=0, help="Décalage pour la pagination (défaut: 0)")
    search_parser.add_argument("--sort-by", default="dateDesc", help="Critère de tri (défaut: dateDesc)")

    args = parser.parse_args()

    config = load_config(args.config)

    host = resolve_setting(args.host, config, "host", required=True)
    user = resolve_setting(args.user, config, "user", required=True)
    password = resolve_setting(args.password, config, "password", required=True)
    insecure = resolve_setting(args.insecure, config, "insecure") or False
    log_file = resolve_setting(args.log_file, config, "log_file")

    setup_logging(log_file)

    if not os.path.isfile(args.config):
        logger.info(
            "Aucun fichier de configuration trouvé à '%s' — usage 100%% ligne de commande.",
            args.config,
        )
    else:
        logger.info("Configuration chargée depuis '%s'.", args.config)

    if insecure:
        urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

    verify = not insecure

    try:
        token, _lifetime = authenticate(host, user, password, verify=verify)

        if args.command == "auth":
            pass  # l'authentification seule suffit, déjà journalisée ci-dessus

        elif args.command == "folders":
            get_folders(host, token, verify=verify, folder_id=args.folder_id)

        elif args.command == "sync":
            sync_folders(host, token, verify=verify, sync_token=args.sync_token, typed=args.typed)

        elif args.command == "message":
            get_message(host, token, args.id, verify=verify, raw=args.raw)

        elif args.command == "search":
            search_messages(
                host,
                token,
                verify=verify,
                query=args.query,
                limit=args.limit,
                offset=args.offset,
                sort_by=args.sort_by,
            )

    except requests.HTTPError as exc:
        logger.error("Erreur HTTP : %s", exc)
        sys.exit(1)
    except Exception:
        logger.exception("Erreur inattendue")
        sys.exit(1)


if __name__ == "__main__":
    main()
