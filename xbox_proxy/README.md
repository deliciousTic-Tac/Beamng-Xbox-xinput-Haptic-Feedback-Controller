# Xbox Haptic Feedback Controller — sources 1.0.24.0

Révision complétée le 24 septembre 2026. `version.h` est la source unique de version.
Les DLL et EXE sont des artefacts générés, exclus du dépôt source.
Recompiler cette version avant de créer un paquet de distribution.
Le candidat **1.0.24.0** est produit dans `out/test` par le pipeline de build.

## Architecture et limites

Le mod Lua canonique est `../revlimiter_haptics/mod`. Le protocole véhicule BCH1
émet 96 octets vers UDP `127.0.0.1:26780`, au maximum à 60 Hz. La DLL est un proxy
x64 XInput1_4 avec sorties WinRT pour les moteurs et les gâchettes. Son pump est
synchrone, appelé depuis XInput, sans worker ni service externe. `DllMain` reste minimal.
Avec le CRT statique `/MT`, il conserve les notifications de threads : aucun appel
à `DisableThreadLibraryCalls`.

Prérequis d'exécution : Windows 10/11 x64, BeamNG x64 et manette compatible WGI.
La compatibilité BeamNG 0.39 mentionnée par le code doit être vérifiée en jeu.
Le format BCH1 et ses ordinals sont conservés. Ne pas mélanger ZIP Lua et DLL
de candidats différents : utiliser le même paquet et son `manifest.json`.

Limites qui restent à valider :

- La sélection WGI compare l'état actif de XInput 0 avec tous les Gamepads WGI.
  Elle exige une correspondance unique confirmée sur deux lectures espacées de
  100 ms minimum. Sans entrée distinctive (bouton, gâchette ou stick) ou en cas
  d'ambiguïté, la DLL conserve la vibration classique XInput et n'envoie pas
  d'effets WGI. La correspondance réelle multimanette reste à tester en jeu.
- Le watchdog de télémétrie (250 ms) dépend des appels XInput. Si ces appels
  cessent, aucun worker indépendant n'envoie de zéro. L'arrêt forcé/crash dépend
  du pilote et de Windows; aucun cleanup de processus ne peut y être garanti.
  Un compagnon autonome fiable devrait piloter lui-même WGI pendant toute la
  session; un simple processus lancé à l'arrêt ne peut pas identifier sûrement
  la même manette. Aucun compagnon persistant ou service n'est installé.
- `XInputEnable(FALSE)` force zéro, inhibe WGI et ferme le socket; TRUE réactive
  les sorties avec la dernière consigne de moteurs ordinaires. Les effets
  télémétriques anciens ne sont pas rejoués. Les jeux ne sont pas tenus d'appeler
  cette API lors de chaque changement de focus sous Windows 10/11.
- La manette appariée est mise en cache seulement si elle annonce `IAgileObject`.
  Le cache est sérialisé, vérifié dans la liste WGI chaque seconde et invalidé au
  retrait, à une erreur de lecture ou de vibration, ou à un désaccord d'entrée.
  Les autres références COM restent locales. Une consigne différente conserve
  la cadence maximale de 16 ms; l'énumération ne tourne plus à cette cadence.
- Tant qu'aucune manette WGI n'est associée et qu'aucune vibration n'est demandée,
  le proxy n'initialise pas WinRT et n'énumère pas les manettes. Cela évite de
  bloquer le thread XInput de BeamNG au démarrage lorsqu'une manette est absente.
  La première consigne non nulle déclenche immédiatement la détection.

## Compiler et vérifier

Installer Visual Studio 2022 / Build Tools, charge **Desktop development with C++**,
toolset x64 et Windows SDK, ainsi que Python 3 et `lupa` (`python -m pip install lupa`).
`HAPTICS_VS_ROOT` permet de préciser une installation MSVC personnalisée.
`HAPTICS_PYTHON` peut désigner le chemin absolu de Python.

Depuis n'importe quel dossier, lancer le script par son chemin :

```powershell
& 'C:\chemin\projet\xbox_proxy\build.cmd' test
```

Le build optimisé (`/O2 /GL /LTCG`, CRT `/MT`) conserve CFG/ASLR/DEP, ajoute
VERSIONINFO, exécute les régressions natives avec WGI simulé et les tests Lua 5.1,
puis vérifie exports, protections, version et identité des entrées de compilation.
Le build échoue si une étape ne passe pas. Il n'écrase pas les anciennes DLL.

Le smoke-test réel, facultatif, se lance explicitement après compilation :

```powershell
& .\out\test\load_test.exe 'C:\chemin\projet\xbox_proxy\out\test\XInput1_4.dll'
```

Il teste STA/MTA et concurrence, appelle la vraie API WGI et demande zéro avant
déchargement. Ne pas le lancer pendant une session BeamNG utilisant le même port.

## Créer un paquet cohérent

Après un build réussi :

```powershell
powershell -NoProfile -File .\tools\artifacts.ps1 -Action Package -Mode test
```

Un dossier `out/test/package-...` reçoit un paquet neuf contenant la DLL,
le ZIP Lua, les sources, un manifeste et les empreintes SHA-256. Chaque entrée
ZIP est relue et vérifiée. Toute modification de source depuis le build ou
substitution de DLL invalide le packaging. `test` signifie **candidat non signé**.

Pour une diffusion signée : checkout Git propre avec commit, certificat Authenticode
déjà disponible avec sa clé privée, `CODE_SIGN_CERT_THUMBPRINT` configuré, puis
`build.cmd release` et `Package -Mode release`. Sans certificat, le mode release
refuse de démarrer. La clé n'est ni créée ni exportée par ces scripts.
`CODE_SIGN_TIMESTAMP_URL` peut remplacer l'URL de timestamp. Le pipeline ne crée
pas automatiquement un dépôt, un commit, un tag ou une publication distante.

## Installation manuelle du candidat et retour arrière

1. Fermer BeamNG. Identifier son dossier réel contenant `Bin64\BeamNG.drive.x64.exe`
   et **son dossier utilisateur actif**, via le launcher BeamNG. Ces deux dossiers
   sont normalement différents : le ZIP va dans le dossier utilisateur, pas dans Steam.
2. Si `Bin64\XInput1_4.dll` existe, conserver une copie datée et son SHA-256 hors
   de Bin64. Ne jamais écraser cette sauvegarde lors d'une installation ultérieure.
   Si la DLL était absente, noter explicitement cette absence.
3. Copier la DLL du paquet validé dans **ce Bin64 uniquement**, jamais dans Windows/System32.
4. Copier le ZIP du sous-dossier `mods` du même paquet vers `mods` du dossier utilisateur.
   Désactiver les anciennes versions du protocole afin d'éviter des paquets concurrents.
5. Activer le mod et **Other protocols** dans les options BeamNG si nécessaire.
   Charger un véhicule et tester F6, vibrations, pause et changement de véhicule.

Pour revenir en arrière, fermer BeamNG, retirer uniquement le ZIP de ce candidat,
puis restaurer la copie exacte de la DLL précédente. Si aucune DLL locale n'était
présente à l'origine, retirer seulement la DLL du candidat. Ne pas supprimer un
fichier préexistant sans pouvoir le restaurer. Une vérification Steam peut restaurer
les fichiers officiels du jeu, mais ne remplace pas une sauvegarde du proxy précédent.

## Réglages et diagnostic

F6 ouvre **Xbox Haptic Feedback Controller**; touche configurable dans Controls.
Les réglages prennent effet au plus après environ 0,1 s de simulation (plus un tick).
L'UI écrit uniquement les valeurs qui changent. Les quatre valeurs `electrics`
publiées sont des indicateurs approximatifs, pas une mesure des moteurs réels.

Logs : `%LOCALAPPDATA%\BeamNG-Controller-Haptics\proxy.log`, horodatés, avec niveau,
limités à 1 Mio puis recommencés. En l'absence de ce chemin ou s'il est trop long,
le log est omis; aucun fallback vers une racine de disque. Les erreurs UDP incluent
le code WinSock et un retry toutes les 5 secondes. Aucune liaison de diagnostic
retour DLL → UI n'est implémentée.

## Contrat et références

- Microsoft : [XInputEnable](https://learn.microsoft.com/en-us/windows/win32/api/xinput/nf-xinput-xinputenable).
- Microsoft : [Gamepad et vibrations](https://learn.microsoft.com/en-us/windows/uwp/gaming/gamepad-and-vibration).
- Microsoft : [DisableThreadLibraryCalls et CRT statique](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-disablethreadlibrarycalls).
- Le [rapport de préparation du dépôt](../AUDIT.md) décrit les sources retenues,
  les exclusions et le contrôle des données sensibles.
