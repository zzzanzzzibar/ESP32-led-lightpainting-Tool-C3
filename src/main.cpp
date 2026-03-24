// =============================================================================
// ESP32-C3 Super Mini — Light Painting Tool - avec interface Web
// WiFi AP : SSID="Lightpainting ESP by JDK" -> 192.168.4.1
//
// GPIO5   LUMIERE    : maintenir = lumiere principale allumee (mode courant)
// GPIO6   MODE       : appui court = mode suivant (cycle toujours)
//                      le slot pattern actif se change depuis l'app web
// GPIO7   POINTEUR   : maintenir = LED repere (position/taille reglable via app)
// GPIO10  LED STRIP  : data WS2812
// GPIO1   ADC BAT    : pont diviseur 10k/10k sur Vbat-18650
//
// LEDs toujours depuis le debut du strip (index 0) :
//   10 leds -> indices 0..9
//   30 leds -> indices 0..29
//   40 leds -> indices 0..39
//   50 leds -> indices 0..49
// =============================================================================
#include <Arduino.h>
#include <FastLED.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "webui.h"

// -----------------------------------------------------------------------------
// HARDWARE — ESP32-C3 Super Mini
// -----------------------------------------------------------------------------
#define LED_PIN        10   // GPIO10 — DATA strip WS2812
#define LED_COUNT_MAX  50

#define PIN_LUMIERE    5    // GPIO5  — hold = ON
#define PIN_MODE       6    // GPIO6  — appui court = mode suivant
#define PIN_POINTEUR   7    // GPIO7  — hold = pointeur / appui long 5s = batterie

// Batterie : pont diviseur 10k/10k sur GPIO1 (ADC1_CH1)
// Brancher sur Vbat-18650 (ENTREE du booster), pas sur la sortie 5V
// BAT+ ── 10k ──┬── GPIO1
//               10k
//               GND
// Vbat = Vadc * 2  (pont 1:1)
#define PIN_BAT        1
#define BAT_R_RATIO    2.0f    // facteur pont diviseur (R1+R2)/R2
#define BAT_SAMPLES    32      // nb de lectures moyennees
#define BAT_V_FULL     4.15f   // Li-ion 18650 pleine charge
#define BAT_V_EMPTY    3.20f   // Li-ion 18650 decharge

// Detection appui long GPIO7 pour affichage batterie (5s)
#define BAT_HOLD_MS    5000    // duree maintien pour afficher batterie
bool     batHoldActif   = false;
uint32_t batHoldDebut   = 0;
bool     batAffichage   = false;  // true = on est en train d'afficher la batterie
uint32_t batAffichageMs = 0;
#define  BAT_AFFICHAGE_DUR_MS  2500   // duree affichage barre batterie

// Valeurs lues periodiquement
float    batVoltage     = 0.0f;
uint8_t  batPercent     = 0;
uint32_t batDernierMs   = 0;
#define  BAT_READ_INTERVAL_MS  10000  // relecture toutes les 10s

#define DEBOUNCE_MS    25

// Niveaux de luminosite (7 paliers : 1/5/10/25/50/80/100 %)
// setMaxPowerInVoltsAndMilliamps() limite automatiquement si besoin
static const uint8_t BRIGHTNESS_LEVELS[7] = { 3, 13, 26, 64, 128, 204, 255 };
#define NB_LUM_LEVELS 7

// Choix valides pour le nombre de LEDs
static const uint8_t NB_LEDS_CHOICES[5] = { 10, 30, 39, 40, 50 };  // mode expert (inchangé)

// Choix taille pinceau en mode Simple : 1, 5, 10, 20, 30, 40 LEDs
static const uint8_t NB_LEDS_SIMPLE[6] = { 1, 5, 10, 20, 30, 40 };
#define NB_LEDS_SIMPLE_COUNT 6

// Paliers de frequence clignotement (periodes en MICROSECONDES)
//   idx 0 = Fix    (pas de clignotement, toujours allume)
//   idx 1 = 5 Hz   (200 000 µs)
//   idx 2 = 25 Hz  ( 40 000 µs)
//   idx 3 = 50 Hz  ( 20 000 µs)
//   idx 4 = 75 Hz  ( ~13 333 µs)
// On utilise micros() pour la precision aux hautes frequences
static const uint32_t BLINK_PERIODS_US[5] = { 0, 200000UL, 40000UL, 20000UL, 13333UL };
#define BLINK_IDX_FIX  0
#define NB_BLINK_FREQS 5

// Vitesses arc-en-ciel : 1, 3, 5, 10, 20 ms par tick
static const uint8_t RAINBOW_SPEEDS[5] = { 1, 3, 5, 10, 20 };

// Positions du point : 0=centre  1=gauche  2=droite  3=deux cotes
// (taille en nb de LEDs configurable dans les reglages communs)
// Taille du point : identique a idxNbLeds (partage les 4 choix 10/30/40/50)
// mais on garde un indice separe pour permettre une taille independante
static const uint8_t TAILLE_POINT_CHOICES[4] = { 1, 10, 30, 50 };

// -----------------------------------------------------------------------------
// PATTERNS PREDEFINIS  (0=off  1=C1  2=C2)
// data[40] = état de chaque LED par position sur 39 LEDs.
// len      = nombre de LEDs utiles (répété si strip plus long).
// Mode défilant : le pattern scroll dans le temps (offset avance).
// Mode statique : le pattern est affiché tel quel (snap).
// -----------------------------------------------------------------------------
struct PatternPredef {
    const char* nom;
    uint8_t     len;
    uint8_t     data[40];
};

// Helpers d'initialisation lisibles (n = cfg.nbLeds() = 39 par défaut)
// On encode pour 39 LEDs ; le renderer répète si n < len.

static const PatternPredef PATTERNS[] = {
// 0 — Tirets : 2 allumées, 4 éteintes (C1) — rythme spatial
{ "Tirets", 6,
  {1,1,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0} },

// 1 — Pointillés : 1 allumée, 5 éteintes
{ "Pointilles", 6,
  {1,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0} },

// 2 — Triangle montant : de 1 LED au centre vers les bords (symétrique)
//     LED 0=bord gauche, 19=centre, 38=bord droit
{ "Triangle", 39,
  {0,0,0,0,0,1,0,0,0,0, 1,0,0,0,1,0,0,0,1,1,
   1,1,0,0,0,1,0,0,0,1, 0,0,0,0,1,0,0,0,0,0} },

// 3 — Croix : barre pleine C1 au milieu + bords C2
{ "Croix", 39,
  {2,0,0,0,0,0,0,0,0,1, 1,1,1,1,1,1,1,1,1,1,
   1,1,1,1,1,1,1,1,1,1, 0,0,0,0,0,0,0,0,0,2} },

// 4 — Onde sinusoïde : une LED allumée par position, position varie sinusoïdalement
//     (on encode la position allumée : toutes à 0 sauf la LED de la "vague")
//     Ici on encode l'onde comme bandes : tiers bas C2, tiers milieu off, tiers haut C1
{ "Sinus", 39,
  {2,2,2,2,2,2,2,2,2,2, 2,2,2,0,0,0,0,0,0,0,
   0,0,0,0,0,0,1,1,1,1, 1,1,1,1,1,1,1,1,1,0} },

// 5 — Zèbre C1/C2 : alternance stricte LED par LED
{ "Zebre", 2,
  {1,2, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0} },

// 6 — Damier large : 3 C1, 3 C2
{ "Damier", 6,
  {1,1,1,2,2,2, 0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0,0,0,
   0,0,0,0,0,0, 0,0,0,0,0,0, 0,0,0,0} },

// 7 — Barres doubles : tiers bas C2, tiers haut C1, tiers milieu off
{ "Barres 2", 39,
  {2,2,2,2,2,2,2,2,2,2, 2,2,2,0,0,0,0,0,0,0,
   0,0,0,0,0,0,1,1,1,1, 1,1,1,1,1,1,1,1,1,0} },

// 8 — Hachures diagonales : la LED allumée avance d'une position à chaque colonne
//     Rendu spécial : on active LED (i % n) — c'est une ligne diagonale dans le spacetime
{ "Hachures", 39,
  {1,0,0,0,0,0,0,0,0,0, 0,1,0,0,0,0,0,0,0,0,
   0,0,1,0,0,0,0,0,0,0, 0,0,0,1,0,0,0,0,0,0} },

// 9 — Eclairs : rafales C1 courtes séparées par silence
{ "Eclairs", 10,
  {1,1,1,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0} },

// 10 — Pulse : animation dynamique centre->bords (len=255 = marqueur rendu special)
{ "Pulse", 255,
  {0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0} },
};
static const uint8_t NB_PATTERNS = sizeof(PATTERNS) / sizeof(PatternPredef);

// -----------------------------------------------------------------------------
// ANIMATIONS
// -----------------------------------------------------------------------------
enum Animation {
    ANIM_STATIQUE = 0,
    ANIM_ARC_EN_CIEL,
    ANIM_PATTERN,
    ANIM_COUNT
};

static const char* NOM_ANIMATIONS[] = {
    "STATIQUE", "ARC_EN_CIEL", "PATTERN"
};

// -----------------------------------------------------------------------------
// CONFIG CENTRALE
// -----------------------------------------------------------------------------
struct Config {
    uint8_t  r1 = 255, g1 = 0,   b1 = 0;    // couleur principale (C1)
    uint8_t  r2 = 0,   g2 = 0,   b2 = 255;  // couleur secondaire + point (C2)

    // Luminosite : indice 0-6 dans BRIGHTNESS_LEVELS (defaut = 4 -> 50%)
    uint8_t  niveauLuminosite  = 4;

    // Nb LEDs : indice 0-4 dans NB_LEDS_CHOICES (defaut = 2 -> 39) — mode expert
    uint8_t  idxNbLeds         = 2;
    // Nb LEDs mode Simple : valeur directe dans NB_LEDS_SIMPLE (defaut = 10)
    uint8_t  idxNbLedsSimple   = 2;   // index dans NB_LEDS_SIMPLE[] -> 10 LEDs

    uint8_t  animation         = ANIM_STATIQUE;

    uint8_t  densite           = 1;     // 1=toutes 2=1/2 3=1/3

    // Frequence clignotement Btn1 (GPIO25 lumiere) en mode Expert : indice 0-4 (defaut=1 -> 5Hz)
    uint8_t  idxFreqBlink      = 1;
    // Frequence clignotement Btn1 (GPIO25 lumiere) en mode Simple : indice 0-4 (defaut=0 -> Fix)
    uint8_t  idxFreqBlinkSimple = 0;
    // Frequence clignotement Btn2 (GPIO27 pointeur) : indice 0-4 dans BLINK_PERIODS_US (defaut=0 -> Fix)
    uint8_t  idxFreqBlinkC2    = 0;

    // Vitesse arc-en-ciel : indice 0-4 dans RAINBOW_SPEEDS (defaut=4 -> 20ms)
    uint8_t  idxVitesseRainbow = 4;

    // Point (GPIO27)
    uint8_t  idxTaillePoint    = 0;   // 0-3 dans TAILLE_POINT_CHOICES
    uint8_t  posPoint          = 0;   // 0=centre 1=gauche 2=droite 3=deux cotes

    // Patterns : 2 slots assignables via bouton physique
    uint8_t  patternSlot1      = 0;   // index dans PATTERNS[]
    uint8_t  patternSlot2      = 2;   // index dans PATTERNS[] (cercle par defaut)
    uint8_t  patternActif      = 0;   // slot courant (0 ou 1)
    uint16_t patternVitesse    = 60;
    bool     patternDefilant   = true;

    // --- helpers ---
    // nbLeds() retourne le nb de LEDs actif selon le mode courant (extern modeExpert)
    uint8_t  nbLeds()        const;  // défini après déclaration de modeExpert
    uint8_t  intensite()     const { return BRIGHTNESS_LEVELS[niveauLuminosite < NB_LUM_LEVELS ? niveauLuminosite : NB_LUM_LEVELS-1]; }
    uint8_t  taillePoint()   const { return TAILLE_POINT_CHOICES[idxTaillePoint < 4 ? idxTaillePoint : 0]; }
    // Retourne la periode en µs pour Btn1 en mode Expert ; 0 = Fix
    uint32_t freqBlinkC1Expert() const {
        return BLINK_PERIODS_US[idxFreqBlink < NB_BLINK_FREQS ? idxFreqBlink : 1];
    }
    // Retourne la periode en µs pour Btn1 en mode Simple ; 0 = Fix
    uint32_t freqBlinkC1Simple() const {
        return BLINK_PERIODS_US[idxFreqBlinkSimple < NB_BLINK_FREQS ? idxFreqBlinkSimple : 0];
    }
    // Retourne la periode en µs pour Btn2 (GPIO27) ; 0 = Fix (toujours allume)
    uint32_t freqBlinkC2()   const {
        return BLINK_PERIODS_US[idxFreqBlinkC2 < NB_BLINK_FREQS ? idxFreqBlinkC2 : 0];
    }
    uint8_t  vitesseRainbow()const { return RAINBOW_SPEEDS[idxVitesseRainbow < 5 ? idxVitesseRainbow : 4]; }
    uint8_t  idxPattern()    const { return (patternActif == 0) ? patternSlot1 : patternSlot2; }
    uint8_t  ledStart()      const { return 0; }
};

Config cfg;

// -----------------------------------------------------------------------------
// PERSISTANCE (NVS)
// -----------------------------------------------------------------------------
Preferences prefs;

// Sauvegarde NVS différée : on évite de bloquer la loop à chaque /set
bool     cfgDirty       = false;
uint32_t cfgDirtyMs     = 0;
#define  CFG_SAVE_DELAY_MS 2000  // sauvegarde 2s après le dernier changement

void sauvegarderConfig() {
    prefs.begin("lp", false);
    prefs.putUChar("r1",   cfg.r1);   prefs.putUChar("g1", cfg.g1); prefs.putUChar("b1", cfg.b1);
    prefs.putUChar("r2",   cfg.r2);   prefs.putUChar("g2", cfg.g2); prefs.putUChar("b2", cfg.b2);
    prefs.putUChar("lum",  cfg.niveauLuminosite);
    prefs.putUChar("leds", cfg.idxNbLeds);
    prefs.putUChar("ledss",cfg.idxNbLedsSimple);
    prefs.putUChar("anim", cfg.animation);
    prefs.putUChar("den",  cfg.densite);
    prefs.putUChar("bfreq", cfg.idxFreqBlink);
    prefs.putUChar("bfreqs",cfg.idxFreqBlinkSimple);
    prefs.putUChar("bfreq2",cfg.idxFreqBlinkC2);
    prefs.putUChar("rain", cfg.idxVitesseRainbow);
    prefs.putUChar("ptsz", cfg.idxTaillePoint);
    prefs.putUChar("ptps", cfg.posPoint);
    prefs.putUChar("ps1",  cfg.patternSlot1);
    prefs.putUChar("ps2",  cfg.patternSlot2);
    prefs.putUChar("pact", cfg.patternActif);
    prefs.putUShort("pvit",cfg.patternVitesse);
    prefs.putBool ("pdef", cfg.patternDefilant);
    prefs.end();
}

// Planifie une sauvegarde NVS dans CFG_SAVE_DELAY_MS ms (évite les freezes à chaque /set)
void scheduleSave() {
    cfgDirty   = true;
    cfgDirtyMs = millis();
}

void chargerConfig() {
    prefs.begin("lp", true);
    if (!prefs.isKey("r1")) { prefs.end(); return; }  // premiere fois : garder defaults
    cfg.r1                 = prefs.getUChar("r1",  cfg.r1);
    cfg.g1                 = prefs.getUChar("g1",  cfg.g1);
    cfg.b1                 = prefs.getUChar("b1",  cfg.b1);
    cfg.r2                 = prefs.getUChar("r2",  cfg.r2);
    cfg.g2                 = prefs.getUChar("g2",  cfg.g2);
    cfg.b2                 = prefs.getUChar("b2",  cfg.b2);
    cfg.niveauLuminosite   = prefs.getUChar("lum", cfg.niveauLuminosite);
    cfg.idxNbLeds          = prefs.getUChar("leds", cfg.idxNbLeds);
    cfg.idxNbLedsSimple    = prefs.getUChar("ledss",cfg.idxNbLedsSimple);
    cfg.animation          = prefs.getUChar("anim",cfg.animation);
    cfg.densite            = prefs.getUChar("den", cfg.densite);
    cfg.idxFreqBlink       = prefs.getUChar("bfreq", cfg.idxFreqBlink);
    cfg.idxFreqBlinkSimple = prefs.getUChar("bfreqs",cfg.idxFreqBlinkSimple);
    cfg.idxFreqBlinkC2     = prefs.getUChar("bfreq2",cfg.idxFreqBlinkC2);
    cfg.idxVitesseRainbow  = prefs.getUChar("rain",cfg.idxVitesseRainbow);
    cfg.idxTaillePoint     = prefs.getUChar("ptsz",cfg.idxTaillePoint);
    cfg.posPoint           = prefs.getUChar("ptps",cfg.posPoint);
    cfg.patternSlot1       = prefs.getUChar("ps1", cfg.patternSlot1);
    cfg.patternSlot2       = prefs.getUChar("ps2", cfg.patternSlot2);
    cfg.patternActif       = prefs.getUChar("pact",cfg.patternActif);
    cfg.patternVitesse     = prefs.getUShort("pvit",cfg.patternVitesse);
    cfg.patternDefilant    = prefs.getBool ("pdef",cfg.patternDefilant);
    prefs.end();
    Serial.println(F("[NVS] Config chargee."));
}

// -----------------------------------------------------------------------------
// PRESETS (3 slots de config complete)
// Nom modifiable depuis l'app, stocke en NVS sous "p0n".."p2n"
// Config du preset sous les memes cles prefixees par "p0_".."p2_"
// -----------------------------------------------------------------------------
#define NB_PRESETS 3
static const char* PRESET_NOMS_DEFAUT[NB_PRESETS] = { "General 1", "General 2", "Test" };

// Serialise cfg dans un namespace NVS "pN"
void sauvegarderPreset(uint8_t slot) {
    if (slot >= NB_PRESETS) return;
    char ns[4]; snprintf(ns, sizeof(ns), "p%u", slot);
    prefs.begin(ns, false);
    prefs.putUChar("r1",  cfg.r1);  prefs.putUChar("g1",cfg.g1); prefs.putUChar("b1",cfg.b1);
    prefs.putUChar("r2",  cfg.r2);  prefs.putUChar("g2",cfg.g2); prefs.putUChar("b2",cfg.b2);
    prefs.putUChar("lum", cfg.niveauLuminosite);
    prefs.putUChar("leds",cfg.idxNbLeds);
    prefs.putUChar("anim",cfg.animation);
    prefs.putUChar("den", cfg.densite);
    prefs.putUChar("bfreq",cfg.idxFreqBlink);
    prefs.putUChar("bfreq2",cfg.idxFreqBlinkC2);
    prefs.putUChar("rain",cfg.idxVitesseRainbow);
    prefs.putUChar("ptsz",cfg.idxTaillePoint);
    prefs.putUChar("ptps",cfg.posPoint);
    prefs.putUChar("ps1", cfg.patternSlot1);
    prefs.putUChar("ps2", cfg.patternSlot2);
    prefs.putUChar("pact",cfg.patternActif);
    prefs.putUShort("pvit",cfg.patternVitesse);
    prefs.putBool ("pdef",cfg.patternDefilant);
    prefs.putBool ("ok",  true);  // marque le slot comme rempli
    prefs.end();
    Serial.printf("[PRESET] Slot %u sauvegarde.\n", slot);
}

// Charge un preset dans cfg (retourne false si slot vide)
bool chargerPreset(uint8_t slot) {
    if (slot >= NB_PRESETS) return false;
    char ns[4]; snprintf(ns, sizeof(ns), "p%u", slot);
    prefs.begin(ns, true);
    if (!prefs.getBool("ok", false)) { prefs.end(); return false; }
    cfg.r1                = prefs.getUChar("r1",  cfg.r1);
    cfg.g1                = prefs.getUChar("g1",  cfg.g1);
    cfg.b1                = prefs.getUChar("b1",  cfg.b1);
    cfg.r2                = prefs.getUChar("r2",  cfg.r2);
    cfg.g2                = prefs.getUChar("g2",  cfg.g2);
    cfg.b2                = prefs.getUChar("b2",  cfg.b2);
    cfg.niveauLuminosite  = prefs.getUChar("lum", cfg.niveauLuminosite);
    cfg.idxNbLeds         = prefs.getUChar("leds",cfg.idxNbLeds);
    cfg.animation         = prefs.getUChar("anim",cfg.animation);
    cfg.densite           = prefs.getUChar("den", cfg.densite);
    cfg.idxFreqBlink      = prefs.getUChar("bfreq",cfg.idxFreqBlink);
    cfg.idxFreqBlinkC2    = prefs.getUChar("bfreq2",cfg.idxFreqBlinkC2);
    cfg.idxVitesseRainbow = prefs.getUChar("rain",cfg.idxVitesseRainbow);
    cfg.idxTaillePoint    = prefs.getUChar("ptsz",cfg.idxTaillePoint);
    cfg.posPoint          = prefs.getUChar("ptps",cfg.posPoint);
    cfg.patternSlot1      = prefs.getUChar("ps1", cfg.patternSlot1);
    cfg.patternSlot2      = prefs.getUChar("ps2", cfg.patternSlot2);
    cfg.patternActif      = prefs.getUChar("pact",cfg.patternActif);
    cfg.patternVitesse    = prefs.getUShort("pvit",cfg.patternVitesse);
    cfg.patternDefilant   = prefs.getBool ("pdef",cfg.patternDefilant);
    prefs.end();
    // Note: FastLED.setBrightness, resetAnim et allumerLeds sont appeles
    // par l'appelant apres chargerPreset() car ils dependent de variables
    // declarees plus loin dans le fichier.
    Serial.printf("[PRESET] Slot %u charge.\n", slot);
    return true;
}

// Lit le nom d'un preset (depuis NVS ou defaut)
void lireNomPreset(uint8_t slot, char* buf, size_t sz) {
    if (slot >= NB_PRESETS) { strncpy(buf, "?", sz); return; }
    char ns[4]; snprintf(ns, sizeof(ns), "p%u", slot);
    prefs.begin(ns, true);
    String n = prefs.getString("nom", PRESET_NOMS_DEFAUT[slot]);
    prefs.end();
    strncpy(buf, n.c_str(), sz - 1); buf[sz - 1] = '\0';
}

// Sauvegarde le nom d'un preset
void ecrireNomPreset(uint8_t slot, const char* nom) {
    if (slot >= NB_PRESETS) return;
    char ns[4]; snprintf(ns, sizeof(ns), "p%u", slot);
    prefs.begin(ns, false);
    prefs.putString("nom", nom);
    prefs.end();
}

inline CRGB couleur1()   { return CRGB(cfg.r1, cfg.g1, cfg.b1); }
inline CRGB couleur2()   { return CRGB(cfg.r2, cfg.g2, cfg.b2); }
inline CRGB couleurPtr() { return couleur2(); }  // le point utilise C2

// -----------------------------------------------------------------------------
// ETAT RUNTIME
// -----------------------------------------------------------------------------
CRGB     leds[LED_COUNT_MAX];
bool     lumiereActive  = false;
bool     pointeurActive = false;
bool     needShow       = false;

int      etatPrecMode   = HIGH;
uint32_t derniereMs26   = 0;

// Mode UI : false = Simple (1 couleur, nb LEDs, intensite) | true = Expert (tout)
// Bascule par appui simultane 3 boutons ~2s. Sauvegarde en NVS namespace "ui".
bool     modeExpert     = false;

// Définition inline de Config::nbLeds() — dépend de modeExpert
inline uint8_t Config::nbLeds() const {
    if (modeExpert)
        return NB_LEDS_CHOICES[idxNbLeds < 5 ? idxNbLeds : 4];
    else
        return NB_LEDS_SIMPLE[idxNbLedsSimple < NB_LEDS_SIMPLE_COUNT ? idxNbLedsSimple : 2];
}

void sauvegarderModeUI() {
    prefs.begin("ui", false);
    prefs.putBool("expert", modeExpert);
    prefs.end();
}

void chargerModeUI() {
    prefs.begin("ui", true);
    modeExpert = prefs.getBool("expert", false);  // defaut = mode simple
    prefs.end();
}

// Detection bascule 3 boutons simultanes (Simple <-> Expert)
bool     tripleActif    = false;
uint32_t tripleDebut    = 0;
#define  TRIPLE_DUR_MS  2000   // duree maintien pour basculer (2s)

// Feedback LED apres changement de mode (GPIO26)
bool     feedbackActif  = false;
uint32_t feedbackMs     = 0;
#define  FEEDBACK_DUR_MS  700   // duree du flash de feedback

// Feedback intensité : barre proportionnelle en blanc (mode Simple, GPIO26 seul)
bool     feedbackIntensiteActif = false;
uint32_t feedbackIntensiteMs    = 0;
#define  FEEDBACK_INT_DUR_MS    500   // duree du feedback intensite

// -----------------------------------------------------------------------------
// MODE SIMPLE — navigation physique
// -----------------------------------------------------------------------------
// 12 couleurs C1 espacées régulièrement sur la roue chromatique (tous les 30°)
// C2 = complémentaire directe (décalée de 180° sur la roue)
// Format : {{R1,G1,B1}, {R2,G2,B2}}
static const uint8_t SIMPLE_COULEURS[13][2][3] = {
    // C1 (hue 0°)    C2 (hue 180°)
    {{255,   0,   0},  {  0, 255, 255}},  //  0 Rouge         ↔ Cyan
    {{255,  64,   0},  {  0, 191, 255}},  //  1 Rouge-Orange  ↔ Bleu ciel
    {{255, 140,   0},  {  0, 115, 255}},  //  2 Orange        ↔ Bleu électrique
    {{255, 220,   0},  { 35,   0, 255}},  //  3 Jaune-Orange  ↔ Bleu-Violet
    {{180, 255,   0},  { 75,   0, 255}},  //  4 Jaune-Vert    ↔ Violet
    {{  0, 255,   0},  {255,   0, 255}},  //  5 Vert          ↔ Magenta
    {{  0, 255, 128},  {255,   0, 128}},  //  6 Vert menthe   ↔ Rose framboise
    {{  0, 255, 255},  {255,   0,   0}},  //  7 Cyan          ↔ Rouge
    {{  0, 128, 255},  {255, 128,   0}},  //  8 Bleu ciel     ↔ Orange
    {{  0,   0, 255},  {255, 220,   0}},  //  9 Bleu          ↔ Jaune
    {{128,   0, 255},  {128, 255,   0}},  // 10 Violet        ↔ Chartreuse
    {{255,   0, 255},  {  0, 255,   0}},  // 11 Magenta       ↔ Vert
    {{255, 255, 255},  {255, 140,   0}},  // 12 Blanc         ↔ Orange
};
#define NB_SIMPLE_COULEURS 13

uint8_t  simpleCouleurIdx  = 0;  // indice courant dans SIMPLE_COULEURS

// Debounce independant pour GPIO26 et GPIO27 en mode simple
uint32_t dernierMs26Simple = 0;
uint32_t dernierMs27Simple = 0;
// Combo GPIO25+GPIO26 : lumiere tenue + appui 26 = cycle nb LEDs
bool     combo2526Actif    = false;
uint32_t combo2526Ms       = 0;
// Combo GPIO25+GPIO27 : lumiere tenue + appui 27 = cycle blink
bool     combo2527Actif    = false;
uint32_t combo2527Ms       = 0;
#define  COMBO_DEBOUNCE_MS  40   // fenetre de debounce relache (ms)

uint32_t animDerniereMs  = 0;   // pour rainbow/pattern (millis)
uint32_t animDerniereUs  = 0;   // oscillateur Btn1 blink (micros)
uint32_t animDerniereUsC2= 0;   // oscillateur Btn2 blink (micros)
bool     animEtatBlink   = false;
bool     animEtatBlinkC2 = false;
uint8_t  animHue         = 0;
int16_t  patternOffset  = 0;

uint32_t dernierDiagMs  = 0;

// -----------------------------------------------------------------------------
// SERVEUR WEB
// -----------------------------------------------------------------------------
AsyncWebServer server(80);

// -----------------------------------------------------------------------------
// PROTOTYPES
// -----------------------------------------------------------------------------
void lireBoutons();
void updateAnimation();
void afficherFeedbackMode();
void afficherFeedbackIntensite();
void allumerLeds();
void eteindreLeds();
void clearLeds();
void resetAnim();
void setupWifi();
void setupServer();
void sendState(AsyncWebServerRequest* req);
void sendPresets(AsyncWebServerRequest* req);
void applyDensite(CRGB color, uint8_t start, uint8_t n);
float   lireBatterie();
uint8_t tensionVersPercent(float v);
void    afficherBatterie();

// -----------------------------------------------------------------------------
// HELPERS LED
// -----------------------------------------------------------------------------
void clearLeds() {
    for (uint8_t i = 0; i < LED_COUNT_MAX; i++) leds[i] = CRGB::Black;
}

void eteindreLeds() { clearLeds(); needShow = true; }

void resetAnim() {
    animEtatBlink    = false;
    animEtatBlinkC2  = false;
    animHue          = 0;
    patternOffset    = 0;
    animDerniereMs   = millis();
    animDerniereUs   = micros();
    animDerniereUsC2 = animDerniereUs;
}

void applyDensite(CRGB color, uint8_t start, uint8_t n) {
    for (uint8_t i = 0; i < n; i++)
        leds[start + i] = (i % cfg.densite == 0) ? color : CRGB::Black;
}

void allumerLeds() {
    uint8_t n     = cfg.nbLeds();
    uint8_t start = cfg.ledStart();
    // Eteindre tout d'abord
    for (uint8_t i = 0; i < LED_COUNT_MAX; i++) leds[i] = CRGB::Black;
    applyDensite(couleur1(), start, n);
    needShow = true;
}

// Eteint les LEDs occupées par le pointeur (pour simuler phase OFF du clignotement)
void eteindreLEDsPointeur() {
    uint8_t barN = cfg.nbLeds();
    uint8_t tp   = cfg.taillePoint();
    if (tp < 1) tp = 1;
    if (tp > barN) tp = barN;
    switch (cfg.posPoint) {
        case 0: { uint8_t c = barN/2; uint8_t s = c - tp/2; uint8_t e = s + tp; for (uint8_t i = s; i < e && i < LED_COUNT_MAX; i++) leds[i] = CRGB::Black; break; }
        case 1: { for (uint8_t i = 0; i < tp && i < LED_COUNT_MAX; i++) leds[i] = CRGB::Black; break; }
        case 2: { uint8_t e = barN; uint8_t s = e - tp; for (uint8_t i = s; i < e && i < LED_COUNT_MAX; i++) leds[i] = CRGB::Black; break; }
        case 3: {
            for (uint8_t i = 0; i < tp && i < LED_COUNT_MAX; i++) leds[i] = CRGB::Black;
            uint8_t e = barN; uint8_t s = e - tp;
            for (uint8_t i = s; i < e && i < LED_COUNT_MAX; i++) leds[i] = CRGB::Black;
            break;
        }
    }
}

void afficherPointeur() {
    uint8_t barStart = cfg.ledStart();   // 0
    uint8_t barN     = cfg.nbLeds();     // nb leds actif
    uint8_t tp       = cfg.taillePoint();
    if (tp < 1)   tp = 1;
    if (tp > barN) tp = barN;

    // Ne pas effacer le buffer : on superpose le point sur l'animation en cours
    switch (cfg.posPoint) {
        case 0: {  // Centre
            uint8_t c = barStart + barN / 2;
            uint8_t s = c - tp / 2;
            uint8_t e = s + tp;
            if (e > LED_COUNT_MAX) { e = LED_COUNT_MAX; s = (e > tp) ? e - tp : 0; }
            for (uint8_t i = s; i < e; i++) leds[i] = couleurPtr();
            break;
        }
        case 1: {  // Extrême gauche
            uint8_t s = barStart;
            uint8_t e = s + tp;
            if (e > barStart + barN) e = barStart + barN;
            for (uint8_t i = s; i < e; i++) leds[i] = couleurPtr();
            break;
        }
        case 2: {  // Extrême droite
            uint8_t e = barStart + barN;
            uint8_t s = e - tp;
            for (uint8_t i = s; i < e; i++) leds[i] = couleurPtr();
            break;
        }
        case 3: {  // Deux côtés
            // Gauche
            uint8_t sL = barStart;
            uint8_t eL = sL + tp;
            if (eL > barStart + barN) eL = barStart + barN;
            for (uint8_t i = sL; i < eL; i++) leds[i] = couleurPtr();
            // Droite
            uint8_t eR = barStart + barN;
            uint8_t sR = eR - tp;
            for (uint8_t i = sR; i < eR; i++) leds[i] = couleurPtr();
            break;
        }
    }
    needShow = true;
}

// -----------------------------------------------------------------------------
// FEEDBACK LED - flash bref apres changement de mode via GPIO26
//   STATIQUE     : 1 LED blanche
//   ARC-EN-CIEL  : 2 LEDs arc-en-ciel (rouge/vert)
//   PATTERN      : 3 LEDs violettes
// Centre sur la barre active, disparait apres FEEDBACK_DUR_MS
// -----------------------------------------------------------------------------
void afficherFeedbackMode() {
    clearLeds();
    uint8_t barStart = cfg.ledStart();
    uint8_t barN     = cfg.nbLeds();
    uint8_t centre   = barStart + barN / 2;

    switch (cfg.animation) {
        case ANIM_STATIQUE: {
            uint8_t i = centre < LED_COUNT_MAX ? centre : 0;
            leds[i] = CRGB::White;
            break;
        }
        case ANIM_ARC_EN_CIEL: {
            uint8_t s = (centre > 1) ? centre - 1 : 0;
            if (s     < LED_COUNT_MAX) leds[s]     = CRGB(220, 30,  30);
            if (s + 1 < LED_COUNT_MAX) leds[s + 1] = CRGB(30,  200, 30);
            if (s + 2 < LED_COUNT_MAX) leds[s + 2] = CRGB(30,  30, 220);
            break;
        }
        case ANIM_PATTERN: {
            uint8_t s = (centre > 1) ? centre - 2 : 0;
            for (uint8_t k = 0; k < 4 && (s + k) < LED_COUNT_MAX; k++)
                leds[s + k] = CRGB(160, 0, 200);
            break;
        }
    }
    needShow = true;
}

// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    // ESP32-C3 USB-CDC : attendre que le port soit pret (jusqu'a 2s max)
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0 < 2000)) { delay(10); }
    delay(100);

    chargerConfig();  // charger la config depuis NVS avant tout
    chargerModeUI();  // charger le mode UI (simple/expert)

    FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, LED_COUNT_MAX);
    FastLED.setBrightness(cfg.intensite());
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 3000); // filet securite 5V/3A
    FastLED.clear();
    FastLED.show();

    // Chenillard rainbow de démarrage
    {
        uint8_t nb = cfg.nbLeds();
        for (uint8_t pass = 0; pass < 2; pass++) {
            for (uint8_t i = 0; i < nb; i++) {
                FastLED.clear();
                leds[i] = CHSV((uint8_t)(i * 255 / nb + pass * 128), 255, 200);
                FastLED.show();
                delay(18);
            }
        }
        FastLED.clear();
        FastLED.show();
    }

    pinMode(PIN_LUMIERE,  INPUT_PULLUP);
    pinMode(PIN_MODE,     INPUT_PULLUP);
    pinMode(PIN_POINTEUR, INPUT_PULLUP);

    // ADC GPIO1 (ADC1_CH1) pour lecture batterie — C3 Super Mini
    // Pont diviseur 10k/10k sur Vbat-18650 (entree booster), pas sur sortie 5V
    analogReadResolution(12);                              // 12 bits → 0-4095
    analogSetPinAttenuation(PIN_BAT, ADC_11db);            // plage ~0-3.6V sur ce pin uniquement
    // Warmup : quelques lectures ignorees (ADC instable au démarrage)
    for (uint8_t i = 0; i < 5; i++) { analogRead(PIN_BAT); delay(5); }
    // Premiere lecture
    batVoltage  = lireBatterie();
    batPercent  = tensionVersPercent(batVoltage);
    batDernierMs = millis();
    Serial.print(F("[BAT INIT] raw_adc="));
    Serial.print((uint32_t)(batVoltage / BAT_R_RATIO / 3.3f * 4095.0f));
    Serial.print(F("  Vadc="));
    Serial.print(batVoltage / BAT_R_RATIO, 3);
    Serial.print(F("V  Vbat="));
    Serial.print(batVoltage, 2);
    Serial.print(F("V  "));
    Serial.print(batPercent);
    Serial.println(F("%"));

    setupWifi();
    setupServer();

    Serial.println(F("=== ESP32-C3 Super Mini Light Painting Tool ==="));
    Serial.println(F("WiFi AP : LightPainting -> 192.168.4.1"));
    Serial.println(F("GPIO5  LUMIERE  : maintenir = lumiere ON"));
    Serial.println(F("GPIO6  MODE     : appui court = mode suivant"));
    Serial.println(F("GPIO7  POINTEUR : maintenir = LED repere"));
    Serial.println(F("GPIO10 LED      : strip WS2812"));
    Serial.println(F("GPIO1  BAT ADC  : pont diviseur 10k/10k"));
    Serial.println(F("Pret."));
}

// -----------------------------------------------------------------------------
// LOOP
// -----------------------------------------------------------------------------
void loop() {
    // Sauvegarde NVS différée : écrire seulement si rien n'a changé depuis CFG_SAVE_DELAY_MS
    if (cfgDirty && (millis() - cfgDirtyMs >= CFG_SAVE_DELAY_MS)) {
        cfgDirty = false;
        sauvegarderConfig();
    }
    lireBoutons();

    uint32_t nowUs = micros();

    // --- Lumiere principale (GPIO25 hold) : C1 clignote a freqBlink1 ---
    bool lumiere25 = (digitalRead(PIN_LUMIERE) == LOW);
    if (lumiere25 && !lumiereActive) {
        lumiereActive = true;
        resetAnim();
        animEtatBlink  = true;   // commence allumé (après resetAnim pour ne pas être écrasé)
        animDerniereUs = micros();
        Serial.print(F("[ON] "));
        Serial.println(NOM_ANIMATIONS[cfg.animation]);
    } else if (!lumiere25 && lumiereActive) {
        lumiereActive = false;
        Serial.println(F("[OFF]"));
    }

    // Oscillateur Btn1 (C1) — actif seulement si lumiere active
    if (lumiereActive) {
        uint32_t periode1 = modeExpert ? cfg.freqBlinkC1Expert() : cfg.freqBlinkC1Simple();
        if (periode1 > 0 && (nowUs - animDerniereUs >= periode1)) {
            animDerniereUs = nowUs;
            animEtatBlink  = !animEtatBlink;
        }
    }

    // --- Pointeur (GPIO27 hold) : actif en mode Expert seulement ---
    bool pointeur27 = modeExpert && (digitalRead(PIN_POINTEUR) == LOW);
    if (pointeur27 && !pointeurActive) {
        pointeurActive   = true;
        animEtatBlinkC2  = true;   // commence allumé
        animDerniereUsC2 = micros();
        Serial.println(F("[POINTEUR ON]"));
    } else if (!pointeur27 && pointeurActive) {
        pointeurActive = false;
        Serial.println(F("[POINTEUR OFF]"));
    }

    // Oscillateur Btn2 (C2) — actif seulement si pointeur actif
    if (pointeurActive) {
        uint32_t periode2 = cfg.freqBlinkC2();
        if (periode2 > 0 && (nowUs - animDerniereUsC2 >= periode2)) {
            animDerniereUsC2 = nowUs;
            animEtatBlinkC2  = !animEtatBlinkC2;
        }
    }

    // --- Rendu ---
    uint32_t periodeC1    = modeExpert ? cfg.freqBlinkC1Expert() : cfg.freqBlinkC1Simple();
    bool phaseC1on = lumiereActive  && ((periodeC1 == 0) ? true : animEtatBlink);
    bool phaseC2on = pointeurActive && ((cfg.freqBlinkC2() == 0) ? true : animEtatBlinkC2);

    if (lumiereActive || pointeurActive) {
        // En mode Simple : toujours statique, quelle que soit cfg.animation
        bool forceStatique = !modeExpert;

        if (forceStatique || cfg.animation == ANIM_STATIQUE) {
            for (uint8_t i = 0; i < LED_COUNT_MAX; i++) leds[i] = CRGB::Black;
            uint8_t nb = cfg.nbLeds();
            uint8_t st = cfg.ledStart();

            // C1 : toute la barre selon densité + blink
            if (phaseC1on) {
                for (uint8_t i = 0; i < nb; i++) {
                    if (i % cfg.densite == 0) leds[st + i] = couleur1();
                }
            }

            // C2 en mode Simple : additif sur toute la barre (pas de pointeur)
            // C2 en mode Expert : pointeur positionné, toujours dessiné même en phase OFF
            if (!modeExpert) {
                if (phaseC2on) {
                    for (uint8_t i = 0; i < nb; i++) {
                        if (i % cfg.densite == 0) leds[st + i] += couleur2();
                    }
                }
            } else if (pointeurActive) {
                afficherPointeur();
                if (!phaseC2on) eteindreLEDsPointeur();
            }
            needShow = true;
        } else {
            // Modes rainbow/pattern : animation sur C1, pointeur superpose C2
            if (lumiereActive) updateAnimation();
            if (pointeurActive) {
                if (!lumiereActive) clearLeds();
                afficherPointeur();
                if (!phaseC2on) eteindreLEDsPointeur();
                needShow = true;
            }
        }
    } else {
        eteindreLeds();
    }

    // Feedback mode : prioritaire pendant FEEDBACK_DUR_MS
    if (feedbackActif) {
        afficherFeedbackMode();
        if (millis() - feedbackMs >= FEEDBACK_DUR_MS) {
            feedbackActif = false;
            if (!lumiereActive && !pointeurActive) eteindreLeds();
            else needShow = true;
        }
    }

    // Feedback intensité (mode Simple, GPIO26) : barre blanche proportionnelle 500ms
    if (feedbackIntensiteActif) {
        afficherFeedbackIntensite();
        if (millis() - feedbackIntensiteMs >= FEEDBACK_INT_DUR_MS) {
            feedbackIntensiteActif = false;
            if (!lumiereActive && !pointeurActive) eteindreLeds();
            else needShow = true;
        }
    }

    // Affichage batterie : bloque le rendu normal pendant BAT_AFFICHAGE_DUR_MS
    if (batAffichage) {
        if (millis() - batAffichageMs >= BAT_AFFICHAGE_DUR_MS) {
            batAffichage = false;
            if (!lumiereActive && !pointeurActive) eteindreLeds();
            else needShow = true;
        }
        // Pendant l'affichage batterie on ne touche pas aux LEDs
        if (needShow) { FastLED.show(); needShow = false; }
        return;
    }

    // Lecture batterie periodique (toutes les BAT_READ_INTERVAL_MS)
    {
        uint32_t now = millis();
        if (now - batDernierMs >= BAT_READ_INTERVAL_MS) {
            batDernierMs = now;
            batVoltage   = lireBatterie();
            batPercent   = tensionVersPercent(batVoltage);
        }
    }

    if (needShow) { FastLED.show(); needShow = false; }
}

// -----------------------------------------------------------------------------
// BOUTONS PHYSIQUES
// -----------------------------------------------------------------------------

// Flash court pour feedback navigation mode simple (1 flash blanc court)
// (conservé pour autres feedbacks eventuels)
void flashFeedbackSimple() {
    for (uint8_t i = 0; i < LED_COUNT_MAX; i++) leds[i] = CRGB::White;
    FastLED.show(); delay(60);
    clearLeds(); FastLED.show();
}

// Feedback intensité mode simple : N LEDs blanches proportionnel au niveau
// Niveau idx sur NB_LUM_LEVELS-1 → affiche (idx+1)*LED_COUNT_MAX/NB_LUM_LEVELS LEDs
void afficherFeedbackIntensite() {
    clearLeds();
    uint8_t idx = cfg.niveauLuminosite < NB_LUM_LEVELS ? cfg.niveauLuminosite : NB_LUM_LEVELS - 1;
    // (idx+1) niveaux / 7 niveaux * 40 LEDs, arrondi
    uint8_t nbAllumees = (uint8_t)(((uint16_t)(idx + 1) * LED_COUNT_MAX + NB_LUM_LEVELS / 2) / NB_LUM_LEVELS);
    if (nbAllumees < 1) nbAllumees = 1;
    if (nbAllumees > LED_COUNT_MAX) nbAllumees = LED_COUNT_MAX;
    for (uint8_t i = 0; i < nbAllumees; i++) leds[i] = CRGB::White;
    needShow = true;
}

// -----------------------------------------------------------------------------
// BATTERIE — lecture ADC + affichage sur LEDs
// Pont diviseur 10k/10k sur GPIO32 (ADC1_CH4)
//   Vbat = Vadc * BAT_R_RATIO
//   Plage : 3.2V (0%) -> 4.15V (100%)
// -----------------------------------------------------------------------------
float lireBatterie() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < BAT_SAMPLES; i++) {
        sum += analogRead(PIN_BAT);
        delay(1);
    }
    float raw  = (float)sum / (float)BAT_SAMPLES;
    float vadc = (raw / 4095.0f) * 3.3f;
    float vbat = vadc * BAT_R_RATIO;
    Serial.printf("[BAT] raw=%.0f  Vadc=%.3fV  Vbat=%.2fV\n", raw, vadc, vbat);
    return vbat;
}

uint8_t tensionVersPercent(float v) {
    if (v >= BAT_V_FULL)  return 100;
    if (v <= BAT_V_EMPTY) return 0;
    return (uint8_t)((v - BAT_V_EMPTY) / (BAT_V_FULL - BAT_V_EMPTY) * 100.0f + 0.5f);
}

// Affiche 5 barres vert→rouge centrees sur la barre active, pendant BAT_AFFICHAGE_DUR_MS
void afficherBatterie() {
    // Du rouge (faible) vers le vert (plein) — index 0 = plus a gauche = charge la plus basse
    static const CRGB BAT_COLORS[5] = {
        CRGB(200,   0,   0),   // seg 0 : rouge      — 1-20%
        CRGB(220,  80,   0),   // seg 1 : orange     — 21-40%
        CRGB(200, 200,   0),   // seg 2 : jaune      — 41-60%
        CRGB( 80, 220,   0),   // seg 3 : vert-jaune — 61-80%
        CRGB(  0, 200,   0),   // seg 4 : vert       — 81-100%
    };

    clearLeds();
    uint8_t nb     = cfg.nbLeds();
    uint8_t st     = cfg.ledStart();
    uint8_t centre = st + nb / 2;

    // Nombre de barres allumees (1-5, minimum 1 si pas completement vide)
    uint8_t barres = (batPercent == 0) ? 0 : (uint8_t)((batPercent + 19) / 20);

    // 5 segments centres : positions centre-2 a centre+2
    for (int8_t k = 0; k < 5; k++) {
        int16_t pos = (int16_t)centre - 2 + k;
        if (pos < 0 || pos >= LED_COUNT_MAX) continue;
        leds[pos] = (k < (int8_t)barres) ? BAT_COLORS[k] : CRGB(20, 20, 20);
    }
    FastLED.show();
    Serial.print(F("[BAT] "));
    Serial.print(batVoltage, 2);
    Serial.print(F("V  "));
    Serial.print(batPercent);
    Serial.println(F("%"));
}

void lireBoutons() {
    uint32_t now = millis();

    if (now - dernierDiagMs >= 3000) {
        dernierDiagMs = now;
        Serial.print(F("[DIAG] "));
        Serial.print(lumiereActive ? F("ON ") : F("OFF "));
        Serial.print(modeExpert ? F("EXPERT ") : F("SIMPLE "));
        Serial.print(NOM_ANIMATIONS[cfg.animation]);
        Serial.print(F(" leds=")); Serial.print(cfg.nbLeds());
        Serial.print(F(" | WiFi AP="));
        Serial.print(WiFi.softAPIP());
        Serial.print(F(" clients="));
        Serial.println(WiFi.softAPgetStationNum());
    }

    bool btn25 = (digitalRead(PIN_LUMIERE)  == LOW);
    bool btn26 = (digitalRead(PIN_MODE)     == LOW);
    bool btn27 = (digitalRead(PIN_POINTEUR) == LOW);

    // --- Detection triple bouton simultane (bascule Simple<->Expert) ---
    // Prioritaire quel que soit le mode courant
    if (btn25 && btn26 && btn27) {
        if (!tripleActif) {
            tripleActif = true;
            tripleDebut = now;
        } else if (now - tripleDebut >= TRIPLE_DUR_MS) {
            tripleActif = false;
            tripleDebut = now + 10000UL;  // anti-rebond 10s
            modeExpert = !modeExpert;
            sauvegarderModeUI();
            // En mode Simple : forcer animation statique
            if (!modeExpert && cfg.animation != ANIM_STATIQUE) {
                cfg.animation = ANIM_STATIQUE;
                sauvegarderConfig();
                resetAnim();
            }
            Serial.print(F("[UI] Mode "));
            Serial.println(modeExpert ? F("EXPERT") : F("SIMPLE"));
            // Flash 2x blanc : bascule
            for (uint8_t k = 0; k < 2; k++) {
                for (uint8_t i = 0; i < LED_COUNT_MAX; i++) leds[i] = CRGB::White;
                FastLED.show(); delay(150);
                clearLeds(); FastLED.show(); delay(100);
            }
        }
        // On a traite le triple, on ne continue pas vers les actions mono/duo
        int etatMode = digitalRead(PIN_MODE);
        etatPrecMode = etatMode;
        return;
    } else {
        tripleActif = false;
    }

    // --- Appui long GPIO27 seul (5s) : afficher niveau batterie sur LEDs ---
    // Prioritaire sur les actions mono-bouton, fonctionne en Simple ET Expert
    if (btn27 && !btn25 && !btn26) {
        if (!batHoldActif) {
            batHoldActif  = true;
            batHoldDebut  = now;
        } else if (!batAffichage && (now - batHoldDebut >= BAT_HOLD_MS)) {
            // Declencher affichage batterie
            batVoltage   = lireBatterie();
            batPercent   = tensionVersPercent(batVoltage);
            batAffichage = true;
            batAffichageMs = now;
            afficherBatterie();
            batHoldDebut = now + 60000UL;   // re-trigger au plus tot dans 1 min
        }
    } else {
        batHoldActif = false;
    }

    int etatMode = digitalRead(PIN_MODE);

    if (modeExpert) {
        // =====================================================================
        // MODE EXPERT
        // GPIO26 appui court seul         = cycle animation
        // GPIO25 hold + GPIO26 appui court = pattern suivant (si ANIM_PATTERN)
        // GPIO27 hold                      = pointeur (gere dans loop())
        // =====================================================================

        bool modeAppuiFront = (etatMode == LOW && etatPrecMode == HIGH
                               && (now - derniereMs26) > DEBOUNCE_MS);

        if (modeAppuiFront) {
            if (btn25 && cfg.animation == ANIM_PATTERN) {
                // LUMIERE tenu + MODE appui = pattern suivant
                uint8_t& slot = (cfg.patternActif == 0) ? cfg.patternSlot1 : cfg.patternSlot2;
                slot = (slot + 1) % NB_PATTERNS;
                resetAnim();
                scheduleSave();
                derniereMs26 = now;
                Serial.print(F("[PATTERN] slot")); Serial.print(cfg.patternActif);
                Serial.print(F("=")); Serial.println(PATTERNS[slot].nom);
                // Flash cyan bref comme feedback
                for (uint8_t i = 0; i < cfg.nbLeds(); i++) leds[cfg.ledStart() + i] = CRGB(0, 180, 180);
                FastLED.show(); delay(80);
                needShow = true;
            } else if (!btn25) {
                // MODE seul = cycle animation
                cfg.animation = (cfg.animation + 1) % ANIM_COUNT;
                Serial.print(F("[MODE] ")); Serial.println(NOM_ANIMATIONS[cfg.animation]);
                derniereMs26  = now;
                feedbackActif = true;
                feedbackMs    = now;
                resetAnim();
                if (!lumiereActive) { clearLeds(); needShow = true; }
            }
        }

    } else {
        // =====================================================================
        // MODE SIMPLE
        // GPIO25 hold                  = lumière C1 (géré dans loop())
        // GPIO27 appui court           = cycle couleur C1 (12 couleurs, une par une)
        // GPIO26 appui court           = cycle intensité (7 niveaux)
        // GPIO25 hold + GPIO27 appui   = cycle fréquence blink
        // GPIO25 hold + GPIO26 appui   = cycle nombre de LEDs (taille pinceau)
        // =====================================================================

        // --- Combo GPIO25+GPIO27 : blink ---
        bool combo2527Actuel = btn25 && btn27 && !btn26;
        if (combo2527Actuel && !combo2527Actif) {
            combo2527Actif = true;
            combo2527Ms    = now;
        } else if (!combo2527Actuel && combo2527Actif) {
            combo2527Actif = false;
            if (now - combo2527Ms >= COMBO_DEBOUNCE_MS) {
                cfg.idxFreqBlinkSimple = (cfg.idxFreqBlinkSimple + 1) % NB_BLINK_FREQS;
                scheduleSave();
                Serial.print(F("[SIMPLE] Blink idx=")); Serial.println(cfg.idxFreqBlinkSimple);
                // Feedback : 1..5 flashes selon l'index
                for (uint8_t k = 0; k <= cfg.idxFreqBlinkSimple; k++) {
                    for (uint8_t i = 0; i < cfg.nbLeds(); i++) leds[cfg.ledStart()+i] = couleur1();
                    FastLED.show(); delay(80);
                    clearLeds(); FastLED.show(); delay(60);
                }
                dernierMs27Simple = now;
            }
        }

        // --- Combo GPIO25+GPIO26 : nombre de LEDs ---
        bool combo2526Actuel = btn25 && btn26 && !btn27;
        if (combo2526Actuel && !combo2526Actif) {
            combo2526Actif = true;
            combo2526Ms    = now;
        } else if (!combo2526Actuel && combo2526Actif) {
            combo2526Actif = false;
            if (now - combo2526Ms >= COMBO_DEBOUNCE_MS) {
                cfg.idxNbLedsSimple = (cfg.idxNbLedsSimple + 1) % NB_LEDS_SIMPLE_COUNT;
                resetAnim();
                scheduleSave();
                Serial.print(F("[SIMPLE] NbLeds=")); Serial.println(cfg.nbLeds());
                // Feedback : allume exactement le nb de LEDs actif
                uint8_t nb = cfg.nbLeds();
                for (uint8_t i = 0; i < LED_COUNT_MAX; i++) leds[i] = CRGB::Black;
                for (uint8_t i = 0; i < nb; i++) leds[cfg.ledStart()+i] = couleur1();
                FastLED.show(); delay(300);
                if (!lumiereActive) { clearLeds(); FastLED.show(); }
                dernierMs26Simple = now;
            }
        }

        // --- GPIO27 seul — cycle couleur C1 (12 couleurs individuelles) ---
        bool btn27Front = (!combo2527Actuel && !combo2526Actuel
                          && btn27 && !btn26
                          && (now - dernierMs27Simple) > DEBOUNCE_MS);
        static bool btn27PrecSimple = false;
        if (btn27Front && !btn27PrecSimple) {
            simpleCouleurIdx = (simpleCouleurIdx + 1) % NB_SIMPLE_COULEURS;
            cfg.r1 = SIMPLE_COULEURS[simpleCouleurIdx][0][0];
            cfg.g1 = SIMPLE_COULEURS[simpleCouleurIdx][0][1];
            cfg.b1 = SIMPLE_COULEURS[simpleCouleurIdx][0][2];
            scheduleSave();
            Serial.print(F("[SIMPLE] Couleur idx=")); Serial.println(simpleCouleurIdx);
            // Feedback : barre entière avec la nouvelle couleur
            uint8_t nb = cfg.nbLeds();
            for (uint8_t i = 0; i < nb; i++) leds[cfg.ledStart()+i] = couleur1();
            FastLED.show(); delay(150);
            if (!lumiereActive) { clearLeds(); FastLED.show(); }
            dernierMs27Simple = now;
        }
        btn27PrecSimple = btn27Front;

        // --- GPIO26 seul — cycle intensité ---
        bool btn26Front = (!combo2526Actuel && !combo2527Actuel
                          && !btn25 && btn26
                          && (now - dernierMs26Simple) > DEBOUNCE_MS
                          && etatMode == LOW && etatPrecMode == HIGH);
        if (btn26Front) {
            cfg.niveauLuminosite = (cfg.niveauLuminosite + 1) % NB_LUM_LEVELS;
            FastLED.setBrightness(cfg.intensite());
            scheduleSave();
            Serial.print(F("[SIMPLE] Lum idx=")); Serial.println(cfg.niveauLuminosite);
            feedbackIntensiteActif = true;
            feedbackIntensiteMs    = now;
            dernierMs26Simple = now;
        }
    }

    etatPrecMode = etatMode;
}

// -----------------------------------------------------------------------------
// ANIMATIONS
// -----------------------------------------------------------------------------
void updateAnimation() {
    uint32_t now   = millis();
    uint8_t  n     = cfg.nbLeds();
    uint8_t  start = cfg.ledStart();

    switch (cfg.animation) {

        case ANIM_STATIQUE:
            break;

        case ANIM_ARC_EN_CIEL: {
            if (now - animDerniereMs < cfg.vitesseRainbow()) break;
            animDerniereMs = now;
            animHue += 2;
            for (uint8_t i = 0; i < LED_COUNT_MAX; i++) leds[i] = CRGB::Black;
            for (uint8_t i = 0; i < n; i++) {
                if (i % cfg.densite == 0)
                    leds[start + i] = CHSV((uint8_t)(animHue + map(i, 0, n-1, 0, 255)), 255, 255);
            }
            needShow = true;
            break;
        }

        case ANIM_PATTERN: {
            const PatternPredef& p = PATTERNS[cfg.idxPattern() % NB_PATTERNS];

            // Pulse : pattern dynamique centre->bords (marqué par len==255)
            if (p.len == 255) {
                if (now - animDerniereMs < cfg.patternVitesse) break;
                animDerniereMs = now;
                for (uint8_t i = 0; i < LED_COUNT_MAX; i++) leds[i] = CRGB::Black;
                uint8_t centre = start + n / 2;
                uint8_t radius = patternOffset % (n / 2 + 1);
                uint8_t lo = (centre >= radius) ? centre - radius : start;
                uint8_t hi = min((uint8_t)(centre + radius), (uint8_t)(start + n - 1));
                for (uint8_t i = lo; i <= hi; i++) leds[i] = couleur1();
                patternOffset++;
                if (patternOffset > n / 2) patternOffset = 0;
                needShow = true;
                break;
            }

            uint8_t plen = (p.len > 0) ? p.len : 1;

            if (cfg.patternDefilant) {
                // Mode défilant : toutes les LEDs allumées selon data[], le pattern scroll
                if (now - animDerniereMs < cfg.patternVitesse) break;
                animDerniereMs = now;
                for (uint8_t i = 0; i < LED_COUNT_MAX; i++) leds[i] = CRGB::Black;
                for (uint8_t i = 0; i < n; i++) {
                    uint8_t cell = (uint8_t)((i + patternOffset) % plen);
                    uint8_t val  = p.data[cell];
                    if      (val == 1) leds[start + i] = couleur1();
                    else if (val == 2) leds[start + i] = couleur2();
                }
                patternOffset = (patternOffset + 1) % plen;
                needShow = true;
            } else {
                // Mode statique : affiche le pattern une seule fois, centré sur la barre
                for (uint8_t i = 0; i < LED_COUNT_MAX; i++) leds[i] = CRGB::Black;
                for (uint8_t i = 0; i < n; i++) {
                    uint8_t val = p.data[i % plen];
                    if      (val == 1) leds[start + i] = couleur1();
                    else if (val == 2) leds[start + i] = couleur2();
                }
                needShow = true;
            }
            break;
        }
    }
}
void setupWifi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(200);  // C3 : laisser la radio s'eteindre completement
    WiFi.mode(WIFI_AP);
    delay(200);  // C3 : laisser le mode AP s'initialiser
    // Canal 1 (plus universel), max 4 clients, pas de mot de passe
    bool ok = WiFi.softAP("Lightpainting ESP by JDK", nullptr, 13, 0, 4);
    Serial.print(F("softAP : ")); Serial.println(ok ? F("OK") : F("FAIL"));
    Serial.print(F("AP IP : "));
    Serial.println(WiFi.softAPIP());
}

// -----------------------------------------------------------------------------
// SERVEUR WEB
// -----------------------------------------------------------------------------
void sendState(AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["r1"]                 = cfg.r1;
    doc["g1"]                 = cfg.g1;
    doc["b1"]                 = cfg.b1;
    doc["r2"]                 = cfg.r2;
    doc["g2"]                 = cfg.g2;
    doc["b2"]                 = cfg.b2;
    doc["niveauLuminosite"]   = cfg.niveauLuminosite;
    doc["idxNbLeds"]          = cfg.idxNbLeds;
    doc["animation"]          = cfg.animation;
    doc["densite"]            = cfg.densite;
    doc["idxFreqBlink"]       = cfg.idxFreqBlink;
    doc["idxFreqBlinkSimple"] = cfg.idxFreqBlinkSimple;
    doc["idxFreqBlinkC2"]     = cfg.idxFreqBlinkC2;
    doc["idxVitesseRainbow"]  = cfg.idxVitesseRainbow;
    doc["idxTaillePoint"]     = cfg.idxTaillePoint;
    doc["posPoint"]           = cfg.posPoint;
    doc["patternSlot1"]       = cfg.patternSlot1;
    doc["patternSlot2"]       = cfg.patternSlot2;
    doc["patternActif"]       = cfg.patternActif;
    doc["patternVitesse"]     = cfg.patternVitesse;
    doc["patternDefilant"]    = cfg.patternDefilant;
    doc["lumiere"]            = lumiereActive;
    doc["pointeur"]           = pointeurActive;
    doc["nbPatterns"]         = NB_PATTERNS;
    doc["modeExpert"]         = modeExpert;
    doc["batteryVoltage"]     = (float)((int)(batVoltage * 10.0f + 0.5f)) / 10.0f;
    doc["batteryPercent"]     = batPercent;
    doc["charging"]           = (batVoltage > BAT_V_FULL + 0.05f);  // >4.2V = chargeur actif

    JsonArray pats = doc["patternsNoms"].to<JsonArray>();
    for (uint8_t i = 0; i < NB_PATTERNS; i++) pats.add(PATTERNS[i].nom);

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

void setupServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send_P(200, "text/html", WEBUI_HTML);
    });

    server.on("/state", HTTP_GET, [](AsyncWebServerRequest* req) {
        sendState(req);
    });

    server.on("/set", HTTP_POST,
        [](AsyncWebServerRequest* req) { req->send(200, "text/plain", "ok"); },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
                req->send(400, "text/plain", "bad json"); return;
            }
            if (doc["r1"].is<int>())               cfg.r1                = doc["r1"];
            if (doc["g1"].is<int>())               cfg.g1                = doc["g1"];
            if (doc["b1"].is<int>())               cfg.b1                = doc["b1"];
            if (doc["r2"].is<int>())               cfg.r2                = doc["r2"];
            if (doc["g2"].is<int>())               cfg.g2                = doc["g2"];
            if (doc["b2"].is<int>())               cfg.b2                = doc["b2"];
            if (doc["niveauLuminosite"].is<int>()) {
                uint8_t v = doc["niveauLuminosite"];
                if (v < NB_LUM_LEVELS) { cfg.niveauLuminosite = v; FastLED.setBrightness(cfg.intensite()); needShow = true; }
            }
            if (doc["idxNbLeds"].is<int>()) {
                uint8_t v = doc["idxNbLeds"];
                if (v < 5) { cfg.idxNbLeds = v; resetAnim(); }
            }
            if (doc["animation"].is<int>()) {
                cfg.animation = doc["animation"];
                resetAnim();
            }
            if (doc["densite"].is<int>())              cfg.densite            = doc["densite"];
            if (doc["idxFreqBlink"].is<int>()) {
                uint8_t v = doc["idxFreqBlink"];
                if (v < NB_BLINK_FREQS) {
                    cfg.idxFreqBlink = v;
                    animDerniereUs   = micros();  // repart proprement depuis maintenant
                    animEtatBlink    = true;       // commence toujours allumé
                }
            }
            if (doc["idxFreqBlinkSimple"].is<int>()) {
                uint8_t v = doc["idxFreqBlinkSimple"];
                if (v < NB_BLINK_FREQS) {
                    cfg.idxFreqBlinkSimple = v;
                    animDerniereUs         = micros();
                    animEtatBlink          = true;
                }
            }
            if (doc["idxFreqBlinkC2"].is<int>()) {
                uint8_t v = doc["idxFreqBlinkC2"];
                if (v < NB_BLINK_FREQS) {
                    cfg.idxFreqBlinkC2 = v;
                    animDerniereUsC2   = micros();  // repart proprement depuis maintenant
                    animEtatBlinkC2    = true;       // commence toujours allumé
                }
            }
            if (doc["idxVitesseRainbow"].is<int>()) {
                uint8_t v = doc["idxVitesseRainbow"];
                if (v < 5) cfg.idxVitesseRainbow = v;
            }
            if (doc["idxTaillePoint"].is<int>()) {
                uint8_t v = doc["idxTaillePoint"];
                if (v < 4) cfg.idxTaillePoint = v;
            }
            if (doc["posPoint"].is<int>()) {
                uint8_t v = doc["posPoint"];
                if (v < 4) cfg.posPoint = v;
            }
            if (doc["patternSlot1"].is<int>()) {
                uint8_t v = doc["patternSlot1"];
                if (v < NB_PATTERNS) { cfg.patternSlot1 = v; resetAnim(); }
            }
            if (doc["patternSlot2"].is<int>()) {
                uint8_t v = doc["patternSlot2"];
                if (v < NB_PATTERNS) { cfg.patternSlot2 = v; resetAnim(); }
            }
            if (doc["patternActif"].is<int>()) {
                uint8_t v = doc["patternActif"];
                if (v < 2) { cfg.patternActif = v; resetAnim(); }
            }
            if (doc["patternVitesse"].is<int>())       cfg.patternVitesse     = doc["patternVitesse"];
            if (doc["patternDefilant"].is<bool>())     cfg.patternDefilant    = doc["patternDefilant"];
            scheduleSave();  // sauvegarde NVS différée pour ne pas bloquer la loop
            req->send(200, "text/plain", "ok");
        }
    );

    // --- Basculer Simple/Expert depuis l'UI ---
    server.on("/setmode", HTTP_POST,
        [](AsyncWebServerRequest* req) { req->send(200, "text/plain", "ok"); },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
                req->send(400, "text/plain", "bad json"); return;
            }
            if (doc["modeExpert"].is<bool>()) {
                modeExpert = doc["modeExpert"];
                sauvegarderModeUI();
            }
            req->send(200, "text/plain", "ok");
        }
    );

    // --- Presets ---
    server.on("/presets", HTTP_GET, [](AsyncWebServerRequest* req) {
        sendPresets(req);
    });

    server.on("/preset/save", HTTP_POST,
        [](AsyncWebServerRequest* req) { req->send(200, "text/plain", "ok"); },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
                req->send(400, "text/plain", "bad json"); return;
            }
            uint8_t slot = doc["slot"] | 255;
            if (slot >= NB_PRESETS) { req->send(400, "text/plain", "bad slot"); return; }
            if (doc["nom"].is<const char*>()) ecrireNomPreset(slot, doc["nom"]);
            sauvegarderPreset(slot);
            req->send(200, "text/plain", "ok");
        }
    );

    server.on("/preset/load", HTTP_POST,
        [](AsyncWebServerRequest* req) { req->send(200, "text/plain", "ok"); },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
                req->send(400, "text/plain", "bad json"); return;
            }
            uint8_t slot = doc["slot"] | 255;
            if (!chargerPreset(slot)) { req->send(404, "text/plain", "empty slot"); return; }
            FastLED.setBrightness(cfg.intensite());
            resetAnim();
            if (lumiereActive) allumerLeds();
            sauvegarderConfig();  // propager la config chargee comme config courante
            sendState(req);       // repondre avec le nouvel etat complet
        }
    );

    server.on("/preset/rename", HTTP_POST,
        [](AsyncWebServerRequest* req) { req->send(200, "text/plain", "ok"); },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
            JsonDocument doc;
            if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
                req->send(400, "text/plain", "bad json"); return;
            }
            uint8_t slot = doc["slot"] | 255;
            if (slot >= NB_PRESETS) { req->send(400, "text/plain", "bad slot"); return; }
            if (doc["nom"].is<const char*>()) ecrireNomPreset(slot, doc["nom"]);
            req->send(200, "text/plain", "ok");
        }
    );

    server.begin();
    Serial.println(F("Serveur web demarre."));
}

void sendPresets(AsyncWebServerRequest* req) {
    JsonDocument doc;
    JsonArray arr = doc["presets"].to<JsonArray>();
    for (uint8_t i = 0; i < NB_PRESETS; i++) {
        char nom[32];
        lireNomPreset(i, nom, sizeof(nom));
        // verifier si le slot est rempli
        char ns[4]; snprintf(ns, sizeof(ns), "p%u", i);
        prefs.begin(ns, true);
        bool ok = prefs.getBool("ok", false);
        prefs.end();
        JsonObject o = arr.add<JsonObject>();
        o["slot"] = i;
        o["nom"]  = nom;
        o["ok"]   = ok;
    }
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
}
