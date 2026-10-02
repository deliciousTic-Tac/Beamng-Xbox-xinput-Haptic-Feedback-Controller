# beamng-revlimit-haptics

Sources du système haptique Xbox pour BeamNG.drive, version **1.0.24.0**.
Le mod Lua transmet la télémétrie du véhicule au proxy XInput x64, qui pilote
les moteurs de la manette et ses gâchettes via Windows.Gaming.Input.

Le dépôt est autonome. Sa structure conserve les chemins relatifs utilisés par
la compilation, les tests et le packaging :

```text
.
├── xbox_proxy/
│   ├── xinput_proxy.cpp
│   ├── xinput_proxy.def
│   ├── version.h / version.rc
│   ├── build.cmd
│   ├── load_test.cpp
│   ├── tests/
│   └── tools/artifacts.ps1
└── revlimiter_haptics/mod/
    ├── lua/ge/extensions/
    ├── lua/vehicle/protocols/controllerHaptics.lua
    ├── scripts/beamng_controller_haptics/modScript.lua
    └── settings/inputmaps/keyboard_controllerHaptics.json
```

## Compiler et créer un paquet

Prérequis : Windows x64, Visual Studio 2022 ou Build Tools avec la charge C++
Desktop et le Windows SDK, Python 3. Les dépendances Python servent aux tests;
elles ne sont pas nécessaires à l'exécution dans BeamNG.

Depuis la racine du dépôt, dans PowerShell :

```powershell
python -m venv .venv
& .\.venv\Scripts\python.exe -m pip install -r .\requirements-test.txt
$env:HAPTICS_PYTHON = (Resolve-Path .\.venv\Scripts\python.exe).Path
& .\xbox_proxy\build.cmd test
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
powershell.exe -NoProfile -File .\xbox_proxy\tools\artifacts.ps1 -Action Package -Mode test
```

Le build reconstruit `xbox_proxy/out/test/XInput1_4.dll` et les exécutables de
test. Le packaging regroupe la DLL, le ZIP Lua correspondant et leurs empreintes.
Tous ces fichiers générés sont ignorés par Git. Le mode `test` produit un candidat
non signé; les conditions du mode `release` sont détaillées dans le
[guide du proxy](xbox_proxy/README.md).

Pour l'exécution : Windows 10/11 x64, BeamNG x64 et une manette compatible WGI.
Les gâchettes haptiques nécessitent une manette équipée de moteurs de gâchette.
Consulter le [guide du proxy](xbox_proxy/README.md) pour l'installation, les
réglages, le diagnostic et les limites de fonctionnement.

## Réseau et fichiers locaux

Le protocole BCH1 transporte 96 octets en UDP vers `127.0.0.1:26780`, à 60 Hz
maximum. Le proxy écoute uniquement sur la boucle locale. Aucune adresse de
réseau privé ni configuration d'infrastructure n'est nécessaire.

Les réglages sont enregistrés par BeamNG; le proxy écrit son journal dans
`%LOCALAPPDATA%\BeamNG-Controller-Haptics\proxy.log`. Les journaux, dumps,
environnements Python, credentials et certificats ne sont pas versionnés.

Le [rapport de préparation du dépôt](AUDIT.md) décrit le périmètre retenu et le
traitement des binaires. L'ancien bridge Python utilise un autre protocole et
ne fait pas partie de cette version.
