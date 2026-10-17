# Syslog_UDP

Client **Syslog UDP (RFC 5424)** « fire-and-forget » pour **ESP8266 / ESP32** (Arduino).
Header-only, aucune allocation dynamique (file d'attente fixe en RAM), envoi
non bloquant depuis `loop()`, fail-soft si le WiFi n'est pas disponible.

Destiné à envoyer logs et dumps de plantage vers un serveur syslog — par exemple
un NAS **OMV** avec un petit listener Python qui écrit un fichier par device.

## Installation (PlatformIO)

Copier le dossier `lib/Syslog_UDP/` dans le projet, **ou** l'ajouter en dépendance
git :

```ini
lib_deps =
    https://github.com/Fo170/Syslog_UDP.git#v1.0.0
```

Puis :

```cpp
#include <Syslog_UDP.h>

Syslog_UDP syslog;

void setup() {
  WiFi.begin(...);
  // après connexion WiFi :
  syslog.begin("192.168.1.209", 5514, "WeMos-Mini-Coulometre");
  syslog.info("Boot OK");
}

void loop() {
  syslog.loop();          // envoie les messages en attente
  syslog.logf(SYSLOG_ERR, "INA226 err=%d", e);
}
```

## API

| Méthode | Description |
|---|---|
| `begin(host, port=5514, hostname="esp8266")` | Configure la destination (non bloquant). `host` doit rester valide (littéral/macro/global). |
| `setEnabled(bool)` / `isEnabled()` | Active/désactive l'envoi. |
| `setFacility(uint8_t)` | Facility syslog (défaut 16 = local0). |
| `setGate(uint32_t ms)` | Cadence minimale entre deux salves. |
| `setAppName(const char*)` | Nom d'application dans la trame (défaut « coulometre »). |
| `log(severity, msg)` | Met un message en file. |
| `logf(severity, fmt, ...)` | Idem avec formatage. |
| `logBlock(severity, text)` | Découpe un texte multi-ligne/long en plusieurs trames. |
| `info/warn/error(msg)` | Raccourcis (severity 6/4/3). |
| `loop()` | Envoie jusqu'à `SYSLOG_BURST` messages (à appeler dans `loop()`). |
| `dropped()` | Nombre de messages perdus (file pleine). |

Sévérités : `SYSLOG_EMERG`(0) … `SYSLOG_DEBUG`(7).

## Format envoyé

```
<PRI>1 - <HOSTNAME> <APP> - - - <MESSAGE>
```

`PRI = facility*8 + severity`. Le champ timestamp est `-` : le serveur horodate
à la réception.

## Réglages compile-time (`-D`)

| Macro | Défaut | Rôle |
|---|---|---|
| `SYSLOG_DEPTH` | 12 | Messages en file avant débordement. |
| `SYSLOG_LEN` | 160 | Taille max d'une trame complète. |
| `SYSLOG_BURST` | 4 | Messages envoyés par appel à `loop()`. |

RAM statique ≈ `SYSLOG_DEPTH × SYSLOG_LEN` octets.

## Version

La version de la librairie est déclarée dans `src/Syslog_UDP.h` :

```c
#define __SYSLOG_UDP_H_VERSION__  "1.0.0"
```

Source **unique** : un projet hôte peut l'agréger (ex. page `/version`) plutôt que
de recopier le numéro.

## Licence

GPL-3.0-only — Olivier FOURNET.
