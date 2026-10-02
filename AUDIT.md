# Préparation du dépôt privé — 2 octobre 2026

## Périmètre

La version retenue est `1.0.24.0`, définie dans `xbox_proxy/version.h`.
Les sources actuelles du proxy et les six fichiers de `revlimiter_haptics/mod`
ont été copiés dans ce dépôt autonome. Le code de fonctionnement, les scripts
de build, les tests et le script de packaging sont conservés sans modification.
Le guide du proxy a été adapté pour retirer les références aux anciens rapports
locaux non copiés. Les sources originales restent dans leurs dossiers initiaux.

Les noms `xbox_proxy` et `revlimiter_haptics` sont conservés à l'intérieur du
dépôt pour respecter les chemins relatifs du build et des tests. Le dossier
Git est uniquement à la racine de ce dépôt `revlimit_haptics`.

## Structure analysée et exclusions

L'inventaire initial couvre 77 fichiers de projet, 46 fichiers de dépendances
de test installées et 4 670 fichiers de l'ancien environnement Python.
Les sources, configurations, scripts, documents, journaux et reçus de build
ont été inspectés. Les archives de distribution ont été ouvertes, y compris
leurs ZIP imbriqués; les chaînes des exécutables et de la DLL ont été examinées.
Les dépendances installées et l'environnement Python ont été inventoriés et
exclus en bloc; ils ne font pas partie des fichiers proposés pour le commit.

Exclus du périmètre copié :

- `xbox_proxy/out/` : compilation, logs, reçus, manifests, paquets et copies
  intermédiaires des sources.
- `xbox_proxy/.test-deps/` : dépendances installées, extensions natives `.pyd`
  et caches; elles se réinstallent avec `requirements-test.txt`.
- Les anciens rapports locaux `COMPTE_RENDU_CROISE_AUDIT_HAPTIQUES.md`,
  `RAPPORT_REVISION_HAPTIQUES.md` et `RAPPORT_POINTS_RESIDUELS_20260922.md` :
  historique de travail, facultatif pour le build.
- `revlimiter_haptics/.venv/` et les caches Python : environnement local,
  contenant notamment des chemins personnels historiques.
- `revlimiter_haptics/bridge/`, ses tests et `BRIDGE_README.md` : ancien bridge
  SDL2 utilisant NRH1, distinct du protocole BCH1 de la version actuelle.
- L'ancien `revlimiter_haptics/README.md` : guide de la version 1.0.15.
- `revlimiter_haptics/revlimiter_haptics_mod.zip` : ancien paquet généré,
  remplacé par les sources Lua et le packaging actuel.

Le `.gitignore` protège également les futurs builds, DLL/EXE, archives, caches,
logs, dumps, sauvegardes, fichiers temporaires, configurations locales,
credentials, clés SSH et matériels de signature.

## Binaires

| Élément | Nécessité | Traitement Git |
| --- | --- | --- |
| Proxy `XInput1_4.dll` | Nécessaire dans `BeamNG.drive/Bin64` à l'exécution | Exclu; reconstruit depuis le C++, les exports et les ressources de version |
| `native_tests.exe`, `load_test.exe` | Validation native et smoke-test facultatif | Exclus; reconstruits par `build.cmd` |
| `.obj`, `.lib`, `.exp`, `.res`, `.pdb`, `.ilk` | Intermédiaires du compilateur et du linker | Exclus; recréés par les outils de build |
| ZIP Lua et ZIP de distribution | Installation et diffusion de la DLL avec le mod correspondant | Exclus; générés par `tools/artifacts.ps1` |
| Extensions Python `.pyd` | Dépendances de test installées | Exclues; réinstallées avec les dépendances Python |
| XInput système et composants WinRT Windows | Dépendances du système d'exploitation | Fournis par Windows; aucune copie dans le dépôt |
| DLL SDL2 et ancien bridge | Ancienne architecture NRH1 | Exclus du périmètre de cette version BCH1 |

Aucun binaire précompilé n'est nécessaire dans le dépôt source. Le CRT est
lié statiquement (`/MT`); les bibliothèques du Windows SDK proviennent du SDK.

## Contrôle des données sensibles

Les contrôles recherchent notamment les signatures de clés privées, tokens,
JWT, credentials, chaînes de connexion, certificats, clés SSH, adresses réseau,
chemins UNC, URLs et chemins locaux personnels. Aucun secret ni élément
d'infrastructure professionnelle n'a été détecté dans les sources retenues.

La seule adresse réseau utilisée est `127.0.0.1`, boucle locale sur le port
UDP 26780. Les chaînes `1.0.24.0` sont des versions, pas des adresses réseau.
Les URLs présentes dans le guide et le build sont la documentation Microsoft
et le service public de timestamp DigiCert.

`CODE_SIGN_CERT_THUMBPRINT` et `CODE_SIGN_TIMESTAMP_URL` sont des noms de
variables d'environnement. Aucun certificat, clé privée, empreinte de
certificat réelle ni credential n'est fourni. Le script de signature utilise
le certificat disponible localement sans exporter sa clé.

Les vérifications initiales ne garantissent pas la sûreté des ajouts futurs :
les fichiers à committer doivent être réexaminés avant publication, y compris
pour un dépôt privé.

## Vérifications du dossier autonome

- 23 fichiers texte retenus, sans binaire précompilé.
- Sources C++, Lua, JSON, tests et scripts de build/packaging identiques aux
  originaux avant normalisation Git des fins de ligne.
- Gitleaks 8.30.1 : aucun secret détecté. L'outil provient de sa release officielle;
  le SHA-256 de l'archive a été contrôlé avant exécution.
- Contrôle complémentaire : aucun chemin personnel absolu, adresse IPv4 privée,
  chemin UNC, credential embarqué, email ou chaîne suspecte à forte entropie.
- 7 tests Lua réussis, 4 tests de garde-fous du packaging réussis; 3 tests sautés
  parce que les DLL nécessaires à leurs fixtures ne sont pas dans le dépôt.
- Syntaxe Python, JSON, PowerShell et grammaire C++ contrôlées.
- MSVC et le Windows SDK ne sont pas disponibles sur le poste de préparation :
  aucune nouvelle compilation native ni validation en jeu n'a été réalisée.

Les métadonnées du commit utilisent le nom du compte GitHub et son adresse
noreply, pour éviter la publication d'une adresse personnelle ou professionnelle.
