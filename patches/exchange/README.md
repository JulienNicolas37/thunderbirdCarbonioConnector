# Patches touchant du code partagé avec Exchange (EWS/Graph)

Ce sous-dossier isole les patches qui touchent — ou ajoutent du code à côté de —
`rust/protocol_shared`, la base partagée avec les modules Exchange/Graph de
`comm-central`. Séparé du reste de `patches/` (spécifique à Carbonio) pour que
ce qui a une vraie implication de non-régression EWS reste bien identifiable,
sans nécessiter un dépôt à part entière pour un nombre de patches aussi faible.

Contexte complet de la décision : `docs/limitations-connues.md`, section
"Hors périmètre phase 1" → "Serveur sortant minimal requis...".
