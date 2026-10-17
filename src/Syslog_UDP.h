/**
 * @file Syslog_UDP.h
 * @brief Client Syslog UDP (RFC 5424) « fire-and-forget » pour ESP8266 / ESP32.
 * @version 1.0.0
 * @author Olivier FOURNET <olivier.fournet@free.fr>
 * @license GPL-3.0-only
 * @see https://github.com/Fo170/Syslog_UDP
 *
 * Envoie des messages Syslog en UDP vers un serveur (ex. OMV + listener Python
 * qui écrit les fichiers de log). Aucune réponse attendue : l'envoi est
 * non bloquant et « best effort ».
 *
 * Principes :
 *   - File d'attente FIXE en RAM (aucun new/delete → zéro fragmentation).
 *   - `log()` / `logf()` / `logBlock()` remplissent la file ; `loop()` envoie
 *     les messages (à appeler depuis loop()).
 *   - Si le WiFi n'est pas connecté, les messages restent en file (fail-soft).
 *   - Format RFC 5424 : `<PRI>1 - HOSTNAME APP - - - MESSAGE`
 *     (timestamp laissé à `-` : le serveur horodate à la réception).
 *
 * ⚠️ `begin()` conserve le pointeur `host` : passer une chaîne STATIQUE (littéral,
 *    macro, buffer global), pas une variable locale/temporaire.
 *
 * Exemple :
 *   Syslog_UDP syslog;
 *   syslog.begin("192.168.1.209", 5514, "WeMos-Mini-Coulometre");
 *   syslog.info("Boot OK");
 *   syslog.logf(SYSLOG_ERR, "INA226 I2C err=%d", e);
 *   // dans loop() :
 *   syslog.loop();
 */

#ifndef SYSLOG_UDP_H
#define SYSLOG_UDP_H

#include <Arduino.h>
#include <WiFiUdp.h>
#if defined(ESP8266)
  #include <ESP8266WiFi.h>
#else
  #include <WiFi.h>
#endif

#define __SYSLOG_UDP_H_VERSION__  "1.0.0"
// ---------------------------------------------------------------------------
// Réglages compile-time (surchargeables via -D)
// ---------------------------------------------------------------------------
// ⚠️ RAM ESP8266 très serrée : DEPTH×LEN est statique. Les RAPPORTS (crash/boot)
// passent par sendBlockNow() (hors file) → la file ne sert qu'au flux live.
#ifndef SYSLOG_DEPTH
#define SYSLOG_DEPTH 4           // nb de messages en file avant débordement
#endif
#ifndef SYSLOG_LEN
#define SYSLOG_LEN 96            // taille max d'une trame complète (octets)
#endif
#ifndef SYSLOG_BURST
#define SYSLOG_BURST 4           // nb de messages envoyés par appel à loop()
#endif

// Sévérités Syslog (RFC 5424 §6.2.1)
#define SYSLOG_EMERG   0
#define SYSLOG_ALERT   1
#define SYSLOG_CRIT    2
#define SYSLOG_ERR     3
#define SYSLOG_WARNING 4
#define SYSLOG_NOTICE  5
#define SYSLOG_INFO    6
#define SYSLOG_DEBUG   7

class Syslog_UDP {
public:
    // Configure la destination. Non bloquant (ne touche pas au réseau ici).
    // `host` DOIT rester valide (littéral/macro/global).
    bool begin(const char* host, uint16_t port = 5514, const char* hostname = "esp8266") {
        _host = host;
        _port = port ? port : 5514;
        strlcpy(_hostname, hostname ? hostname : "esp8266", sizeof(_hostname));
        _enabled = true;
        _ipSet   = false;
        _head    = 0;
        _count   = 0;
        _dropped = 0;
        _lastSend = 0;
        return true;
    }

    void setEnabled(bool e)        { _enabled = e; }
    bool isEnabled() const         { return _enabled; }
    void setFacility(uint8_t f)    { _facility = (uint8_t)(f & 0x1F); }
    void setGate(uint32_t ms)      { _gate = ms; }
    void setAppName(const char* a) { strlcpy(_app, a ? a : "coulometre", sizeof(_app)); }
    uint32_t dropped() const       { return _dropped; }

    // --- Mise en file ---
    void log(uint8_t severity, const char* msg) {
        if (!_enabled || !msg || !*msg) return;
        uint16_t pri = (uint16_t)((_facility << 3) | (severity & 0x07));
        if (_count == SYSLOG_DEPTH) {          // file pleine → drop le plus ancien
            _head = (uint8_t)((_head + 1) % SYSLOG_DEPTH);
            _count--;
            _dropped++;
        }
        uint8_t idx = (uint8_t)((_head + _count) % SYSLOG_DEPTH);
        snprintf(_q[idx], SYSLOG_LEN, "<%u>1 - %s %s - - - %s",
                 (unsigned)pri, _hostname, _app, msg);
        _count++;
    }

    void logf(uint8_t severity, const char* fmt, ...) {
        if (!_enabled) return;
        char b[SYSLOG_LEN];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(b, sizeof(b), fmt, ap);
        va_end(ap);
        log(severity, b);
    }

    // Découpe un texte multi-ligne / long en plusieurs trames (évite troncature).
    void logBlock(uint8_t severity, const char* text) {
        if (!_enabled || !text) return;
        const size_t maxChunk = (SYSLOG_LEN > 64) ? (SYSLOG_LEN - 48) : 16;
        const char* p = text;
        char line[SYSLOG_LEN];
        while (*p) {
            const char* nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            while (len && (p[len - 1] == '\r' || p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
            size_t off = 0;
            while (off < len) {
                size_t n = len - off;
                if (n > maxChunk) n = maxChunk;
                memcpy(line, p + off, n);
                line[n] = '\0';
                log(severity, line);
                off += n;
            }
            if (!nl) break;
            p = nl + 1;
        }
    }

    void info(const char* m)  { log(SYSLOG_INFO, m); }
    void warn(const char* m)  { log(SYSLOG_WARNING, m); }
    void error(const char* m) { log(SYSLOG_ERR, m); }

    // Envoi SYNCHRONE (hors file) d'un texte multi-ligne/long, découpé en trames.
    // À utiliser pour les RAPPORTS (ex. dump de crash) qui dépassent SYSLOG_DEPTH :
    // la file ne peut pas les absorber. À appeler dans setup()/loop(), PAS en
    // contexte async (fait un DNS + des send() bloquants brefs).
    // Si le WiFi/DNS n'est pas prêt, repli sur la file (logBlock).
    void sendBlockNow(uint8_t severity, const char* text) {
        if (!_enabled || !text || !*text) return;
        if (WiFi.status() != WL_CONNECTED || (!_ipSet && !WiFi.hostByName(_host, _ip))) {
            logBlock(severity, text);
            return;
        }
        _ipSet = true;
        uint16_t pri = (uint16_t)((_facility << 3) | (severity & 0x07));
        const size_t maxChunk = (SYSLOG_LEN > 64) ? (SYSLOG_LEN - 48) : 16;
        const char* p = text;
        char line[SYSLOG_LEN];
        char frame[SYSLOG_LEN];
        while (*p) {
            const char* nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            while (len && (p[len - 1] == '\r' || p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
            size_t off = 0;
            while (off < len) {
                size_t n = len - off;
                if (n > maxChunk) n = maxChunk;
                memcpy(line, p + off, n);
                line[n] = '\0';
                snprintf(frame, sizeof(frame), "<%u>1 - %s %s - - - %s",
                         (unsigned)pri, _hostname, _app, line);
                _send(frame);
                off += n;
            }
            if (!nl) break;
            p = nl + 1;
        }
    }

    // À appeler dans loop() : envoie jusqu'à SYSLOG_BURST messages.
    void loop() {
        if (!_enabled || _count == 0) return;
        if (WiFi.status() != WL_CONNECTED) return;      // garde la file
        if (!_ipSet) {
            if (!WiFi.hostByName(_host, _ip)) return;    // DNS pas prêt → retry
            _ipSet = true;
        }
        if ((uint32_t)(millis() - _lastSend) < _gate) return;
        _lastSend = millis();
        for (uint8_t i = 0; i < SYSLOG_BURST && _count > 0; i++) {
            _send(_q[_head]);
            _head = (uint8_t)((_head + 1) % SYSLOG_DEPTH);
            _count--;
        }
    }

private:
    void _send(const char* msg) {
        _udp.beginPacket(_ip, _port);
        _udp.write((const uint8_t*)msg, strlen(msg));
        _udp.endPacket();
    }

    WiFiUDP      _udp;
    IPAddress    _ip;
    const char*  _host = nullptr;
    uint16_t     _port = 5514;
    char         _hostname[24] = "esp8266";
    char         _app[14]      = "coulometre";
    uint8_t      _facility = 16;   // local0
    bool         _enabled  = false;
    bool         _ipSet    = false;
    uint32_t     _gate     = 20;    // ms mini entre 2 salves
    uint32_t     _lastSend = 0;
    uint32_t     _dropped  = 0;
    uint8_t      _head     = 0;
    uint8_t      _count    = 0;
    char         _q[SYSLOG_DEPTH][SYSLOG_LEN];
};

#endif // SYSLOG_UDP_H
