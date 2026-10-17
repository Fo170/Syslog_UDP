# Changelog - Syslog_UDP

## [1.0.0] - 2026-10-17

Version initiale.

### ✨ Added
- Client **Syslog UDP RFC 5424** « fire-and-forget » (aucun blocage : envoi UDP
  sans attente d'acquittement).
- `log()` / `logf()` / `logBlock()` — messages simples, formatés, ou blocs.
- File d'attente bornée (`SYSLOG_DEPTH`, `SYSLOG_LEN` — surchargables par
  `build_flags`).
- `sendBlockNow()` — dump synchrone découpé (utilisé pour le rapport de crash,
  qui ne peut pas passer par la file).
- `syslogBootReport()` / `loopCrashReport()` — rapport de boot et rapport de
  crash **différé** (après quelques secondes, avec garde de heap).
- `setHost()` / `setPort()` — destination (ex. serveur OMV `192.168.1.209:5514`).

### Usage typique
```cpp
Syslog_UDP syslog;
syslog.begin(SYSLOG_HOST, SYSLOG_PORT, "coulometre");
sw.setLogSink([](const char* l){ syslog.log(LOG_INFO, l); });  // miroir SerialWeb
syslog.loop();   // vide la file (hors OTA)
```

### Côté serveur (OMV)
`tools/iot_syslog.py` (+ `iot_syslog.json.example`, `iot-syslog.service`) écrit
`hdd1/LOG/<hostname>.log` avec rotation, rétention, `crashes.log` et alertes
pluggables.
