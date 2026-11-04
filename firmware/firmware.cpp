/**
 * ============================================================
 * HIDE · Hardware Input Dynamic Emulator · Firmware v1.0
 * Raspberry Pi Pico (RP2040)
 * Pass-Through + Rapid-Fire + Recoil Control naturalizados
 *  REQUISITO DE BUILD: Tools > USB Stack = "Adafruit TinyUSB" e CPU Speed = 240 MHz.
 *  (PIO-USB exige clock múltiplo de 12 MHz; 125 MHz padrão NÃO funciona.)
 * ============================================================
 *
 * Novidades v3.7 (sobre v3.6):
 *  - FIX de timing: CPS agora é CPS real (13 CPS = 13 cliques/s)
 *    Antes: ciclo = hold + interval = 14 + 77 = 91 ms → 11 CPS
 *    Agora: intervalo = target - hold → ciclo = 77 ms → 13 CPS
 *  - Fix de viés do biologicalOffset: clamp simétrico [-20, +20]
 *  - LED metrônomo agora pisca na taxa de CPS (não metade)
 *  - settingsDirty mantido (evita wear da EEPROM no scroll)
 *  - Limite máximo: 20 CPS (50 ms / ciclo)
 *
 * HTML atual é compatível sem alterações no formato de configuração (v3).
 * ============================================================
 */

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <pio_usb.h>
#include <hardware/clocks.h>
#include <hardware/watchdog.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <math.h>

#include <atomic>
#include <cstdint>

// ============================================================
// HARDWARE
// ============================================================
constexpr uint8_t  HOST_USB_DP_PIN = 0;

// ============================================================
// FILA SPSC
// ============================================================
constexpr uint8_t QUEUE_SIZE = 128;
constexpr uint8_t QUEUE_MASK = QUEUE_SIZE - 1;
static_assert((QUEUE_SIZE & QUEUE_MASK) == 0, "QUEUE_SIZE deve ser potencia de 2");

struct __attribute__((packed)) MouseReport {
    uint8_t buttons;
    int8_t  x;
    int8_t  y;
    int8_t  wheel;
    int8_t  pan;
};
static_assert(sizeof(MouseReport) == 5, "Tamanho inesperado do relatorio HID");

// ============================================================
// USB
// ============================================================
uint8_t const mouseReportDescriptor[] = { TUD_HID_REPORT_DESC_MOUSE() };
Adafruit_USBD_HID usbMouse(
    mouseReportDescriptor, sizeof(mouseReportDescriptor),
    HID_ITF_PROTOCOL_MOUSE, 2, false);
Adafruit_USBH_Host USBHost;

// ============================================================
// CONFIG ORGANIC
// ============================================================
struct OrganicFastFire {
    bool uniform        = true;
    bool gaussian       = true;
    bool randomwalk     = true;
    bool jitterUniform  = true;
};

struct OrganicReaction {
    bool     enabled      = true;
    uint32_t minMs        = 100;
    uint32_t maxMs        = 380;
    float    medianMs     = 200.0f;
    float    sigma        = 0.25f;
    uint32_t distractPct  = 3;
    uint32_t distractAdd  = 250;
};

struct OrganicLognormal {
    bool  enabled       = true;
    float stepMedianMs  = 50.0f;
    float stepSigma     = 0.35f;
    uint32_t stepMinMs  = 25;
    uint32_t stepMaxMs  = 150;
};

struct OrganicPink {
    bool     enabled   = true;
    uint32_t tauAMs    = 800;
    uint32_t tauBMs    = 400;
    uint32_t tauCMs    = 200;
    float    weightA   = 0.25f;
    float    weightB   = 0.33f;
    float    weightC   = 0.42f;
    float    rateMod   = 0.30f;
};

struct OrganicJitter {
    bool  enabled = true;
    float scale   = 0.05f;  // desvio padrao em px por relatorio
};

struct OrganicTremor {
    bool  enabled = true;
    float hz      = 10.0f;
    float amp     = 0.4f;  // amplitude em px, aplicada diretamente
};

struct OrganicOvershoot {
    bool     enabled    = true;
    uint32_t pct        = 15;
    float    minMag     = 0.03f;
    float    maxMag     = 0.12f;
    float    correction = 0.5f;  // duracao da reversao (fracao do passo)
};

struct OrganicFitts {
    bool  enabled = true;
    float scale   = 0.30f;
    float maxVal  = 0.50f;
};

struct OrganicFatigue {
    bool     enabled = true;
    uint32_t halfMs  = 60000;  // constante de tempo da saturacao exponencial
    float    maxVal  = 0.60f;
};

struct OrganicPause {
    bool     enabled           = true;
    uint32_t pct               = 2;
    uint32_t minMs             = 30;
    uint32_t maxMs             = 90;
    float    reactiveDriftPx   = 6.0f;
    uint32_t reactivePausePct  = 15;
    float    reactiveDamp      = 0.70f;
};

struct OrganicConfig {
    OrganicFastFire   fastFire;
    OrganicReaction   reaction;
    OrganicLognormal  lognormal;
    bool              ornsteinUhlenbeck = true;
    OrganicPink       pink;
    bool              smoothstep        = true;
    bool              bezier2           = true;
    OrganicJitter     jitterGauss;
    OrganicTremor     tremor10hz;
    OrganicOvershoot  overshoot;
    OrganicFitts      fitts;
    OrganicFatigue    fatigue;
    OrganicPause      pause;
};

static OrganicConfig organic;

// ============================================================
// ESTADO GLOBAL
// ============================================================
static MouseReport reportQueue[QUEUE_SIZE];
static std::atomic<uint8_t>  queueHead{0};
static std::atomic<uint8_t>  queueTail{0};
static std::atomic<uint32_t> disconnectGeneration{0};
static std::atomic<uint32_t> queueOverflowCount{0};
static bool     releasePending = true;
static uint32_t observedDisconnectGeneration = 0;
static volatile bool hidReportInFlight = false;

static bool     rapidFireEnabled  = false;
static bool     settingsDirty     = false;

// Último estado conhecido do mouse + estado do botão do meio (Core 0)
static uint8_t  lastButtons       = 0;
static uint32_t middlePressStart  = 0;
static bool     toggleTriggered   = false;
static uint32_t lastLedToggle     = 0;
static bool     ledPulseState     = false;

// Fast-fire
static bool     isCurrentlyDown   = false;
static uint32_t lastActionTime    = 0;
static uint32_t currentInterval   = 77;
static uint32_t currentHoldTime   = 14;
static int32_t  biologicalOffset  = 0;
static uint32_t targetBaseInterval = 77; // ciclo alvo em ms (target = 1000/cps)

// Recoil Control
static bool     recoilEnabled     = false;
static float    recoilYDown       = 0.0f;
static float    recoilYUp         = 0.0f;
static float    recoilXLeft       = 0.0f;
static float    recoilXRight      = 0.0f;
static uint8_t  recoilTriggerMask = 0x03;

// Burst state
static uint32_t burstStartTime    = 0;
static uint32_t lastRecoilTime    = 0;
static float    recoilAccumX      = 0.0f;
static float    recoilAccumY      = 0.0f;

// Step atual
static uint32_t recoilStepStartTime = 0;
static uint32_t recoilStepDuration  = 0;
static uint32_t recoilNextStep      = 0;
static float    recoilStepStartX    = 0.0f;
static float    recoilStepStartY    = 0.0f;
static float    recoilStepTargetX   = 0.0f;
static float    recoilStepTargetY   = 0.0f;
static float    recoilBezierCtrlX   = 0.0f;
static float    recoilBezierCtrlY   = 0.0f;
static float    recoilPrevBezierX   = 0.0f;
static float    recoilPrevBezierY   = 0.0f;

// Ruído 1/f
static float    recoilNoiseA = 0.0f;
static float    recoilNoiseB = 0.0f;
static float    recoilNoiseC = 0.0f;
static uint32_t recoilNoiseTimeA = 0;
static uint32_t recoilNoiseTimeB = 0;
static uint32_t recoilNoiseTimeC = 0;

// Pausa e overshoot
static uint32_t recoilPauseUntil      = 0;
static bool     recoilOvershoot       = false;
static float    recoilOvershootMagX   = 0.0f;
static float    recoilOvershootMagY   = 0.0f;
static float    recoilOvershootPrevX  = 0.0f;
static float    recoilOvershootPrevY  = 0.0f;
static uint32_t recoilOvershootStart  = 0;
static uint32_t recoilOvershootEnd    = 0;
static float    recoilTremorPrevX     = 0.0f;
static float    recoilTremorPrevY     = 0.0f;

// ============================================================
// EEPROM
// ============================================================
constexpr uint32_t EEPROM_MAGIC = 0x48494407; // HID v7
constexpr int      EEPROM_SIZE  = 512;

static StaticJsonDocument<12288> jsonDoc;

// ============================================================
// PROTÓTIPOS
// ============================================================
static long   getGaussianRandom(long mean, long deviation);
static float  gaussFloat(float mean, float stddev);
static uint32_t logNormalSample(float medianMs, float sigma,
                                uint32_t minMs, uint32_t maxMs);
static bool   enqueueReport(const MouseReport&);
static bool   dequeueReport(MouseReport&);
static void   clearReportQueue();
static void   requestNeutralReport();
static void   serviceNeutralReport();
static bool   applyJsonConfig(JsonObjectConst);
static void   sendCurrentConfig();
static void   processSerialCommands();
static bool   isRecoilTriggerActive(uint8_t);
static void   applyRecoilControl(MouseReport&, uint8_t, uint32_t);
static void   resetRecoilBurst();
static void   resetFastFireState();
static void   loadSettings();
static void   saveSettings();

// ============================================================
// TinyUSB callback
// ============================================================
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const*, uint16_t) {
    (void)instance;
    hidReportInFlight = false;
}

// ============================================================
// Fila SPSC
// ============================================================
static bool enqueueReport(const MouseReport& r) {
    const uint8_t head = queueHead.load(std::memory_order_relaxed);
    const uint8_t next = (head + 1) & QUEUE_MASK;
    if (next == queueTail.load(std::memory_order_acquire)) {
        queueOverflowCount.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    reportQueue[head] = r;
    queueHead.store(next, std::memory_order_release);
    return true;
}
static bool dequeueReport(MouseReport& r) {
    const uint8_t tail = queueTail.load(std::memory_order_relaxed);
    if (tail == queueHead.load(std::memory_order_acquire)) return false;
    r = reportQueue[tail];
    queueTail.store((tail + 1) & QUEUE_MASK, std::memory_order_release);
    return true;
}
static void clearReportQueue() {
    queueTail.store(queueHead.load(std::memory_order_acquire), std::memory_order_release);
}

// ============================================================
// Neutral report
// ============================================================
static void requestNeutralReport() { clearReportQueue(); releasePending = true; }

static void serviceNeutralReport() {
    if (!releasePending || !TinyUSBDevice.mounted()) return;
    if (hidReportInFlight || !usbMouse.ready()) return;
    const MouseReport n{};
    hidReportInFlight = true;
    if (usbMouse.sendReport(0, &n, sizeof(n))) releasePending = false;
    else hidReportInFlight = false;
}

// ============================================================
// Estatística
// ============================================================
static long getGaussianRandom(long mean, long deviation) {
    // Gaussiana de verdade (Box-Muller), com desvio padrao = deviation.
    return (long)llroundf(gaussFloat((float)mean, (float)deviation));
}

static float gaussFloat(float mean, float stddev) {
    const float u1 = random(1, 32767) / 32768.0f;
    const float u2 = random(1, 32767) / 32768.0f;
    const float z  = sqrtf(-2.0f * logf(u1)) * cosf(2.0f * PI * u2);
    return mean + stddev * z;
}

static uint32_t logNormalSample(float medianMs, float sigma,
                                uint32_t minMs, uint32_t maxMs) {
    const float z  = gaussFloat(0.0f, 1.0f);
    const float ms = expf(logf(medianMs) + sigma * z);
    return (uint32_t)constrain((long)ms, (long)minMs, (long)maxMs);
}

// ============================================================
// RECOIL · reset
// ============================================================
static void resetRecoilBurst() {
    burstStartTime       = 0;
    lastRecoilTime       = 0;
    recoilAccumX = recoilAccumY = 0.0f;
    recoilStepStartTime  = 0;
    recoilStepDuration   = 0;
    recoilNextStep       = 0;
    recoilStepStartX = recoilStepStartY = 0.0f;
    recoilStepTargetX = recoilStepTargetY = 0.0f;
    recoilBezierCtrlX = recoilBezierCtrlY = 0.0f;
    recoilPrevBezierX = recoilPrevBezierY = 0.0f;
    recoilNoiseA = recoilNoiseB = recoilNoiseC = 0.0f;
    recoilPauseUntil = 0;
    recoilOvershoot = false;
    recoilOvershootMagX = recoilOvershootMagY = 0.0f;
    recoilOvershootPrevX = recoilOvershootPrevY = 0.0f;
    recoilOvershootStart = 0;
    recoilOvershootEnd = 0;
    recoilTremorPrevX = 0.0f;
    recoilTremorPrevY = 0.0f;
}

// ============================================================
// RECOIL · gatilho
// ============================================================
static bool isRecoilTriggerActive(uint8_t b) {
    switch (recoilTriggerMask) {
        case 0x03: return (b & 0x03) == 0x03;
        case 0x02: return (b & 0x02) != 0;
        case 0x01:
        default:   return (b & 0x01) != 0;
    }
}

// ============================================================
// RECOIL · todas as camadas
// ============================================================
static void applyRecoilControl(MouseReport& report, uint8_t rawButtons, uint32_t now) {
    if (!recoilEnabled) { resetRecoilBurst(); return; }

    const float baseDx = recoilXRight - recoilXLeft;
    const float baseDy = recoilYDown  - recoilYUp;
    if (fabsf(baseDx) < 0.001f && fabsf(baseDy) < 0.001f) return;

    if (!isRecoilTriggerActive(rawButtons)) { resetRecoilBurst(); return; }

    // ---------- Início do burst: delay de reação ----------
    if (lastRecoilTime == 0) {
        burstStartTime   = now;
        lastRecoilTime   = now;
        recoilNoiseTimeA = now - organic.pink.tauAMs;
        recoilNoiseTimeB = now - organic.pink.tauBMs;
        recoilNoiseTimeC = now - organic.pink.tauCMs;

        uint32_t reaction = 0;
        if (organic.reaction.enabled) {
            reaction = logNormalSample(
                organic.reaction.medianMs, organic.reaction.sigma,
                organic.reaction.minMs, organic.reaction.maxMs);
            if (random(0, 100) < (long)organic.reaction.distractPct)
                reaction += random(0, organic.reaction.distractAdd);
        }

        recoilStepStartTime = 0;
        recoilStepDuration  = 0;
        recoilNextStep      = now + reaction;
        recoilPrevBezierX   = 0.0f;
        recoilPrevBezierY   = 0.0f;
        return;
    }

    const uint32_t elapsed = now - lastRecoilTime;
    if (elapsed == 0) return;
    lastRecoilTime = now;

    // ---------- 1/f (3 OU) ----------
    float pinkRate  = 0.0f;
    float pinkCurve = 0.0f;
    if (organic.ornsteinUhlenbeck && organic.pink.enabled) {
        const float dtA = (float)(now - recoilNoiseTimeA) / 1000.0f;
        if (dtA > 0.0f) {
            const float aA = expf(-dtA / (organic.pink.tauAMs / 1000.0f));
            const float nA = aA * recoilNoiseA + sqrtf(1.0f - aA * aA) * gaussFloat(0.0f, 1.0f);
            recoilNoiseA = constrain(nA, -1.0f, 1.0f);
            recoilNoiseTimeA = now;
        }
        const float dtB = (float)(now - recoilNoiseTimeB) / 1000.0f;
        if (dtB > 0.0f) {
            const float aB = expf(-dtB / (organic.pink.tauBMs / 1000.0f));
            const float nB = aB * recoilNoiseB + sqrtf(1.0f - aB * aB) * gaussFloat(0.0f, 1.0f);
            recoilNoiseB = constrain(nB, -1.0f, 1.0f);
            recoilNoiseTimeB = now;
        }
        const float dtC = (float)(now - recoilNoiseTimeC) / 1000.0f;
        if (dtC > 0.0f) {
            const float aC = expf(-dtC / (organic.pink.tauCMs / 1000.0f));
            const float nC = aC * recoilNoiseC + sqrtf(1.0f - aC * aC) * gaussFloat(0.0f, 1.0f);
            recoilNoiseC = constrain(nC, -1.0f, 1.0f);
            recoilNoiseTimeC = now;
        }
        pinkRate  = organic.pink.weightA * recoilNoiseA
                  + organic.pink.weightB * recoilNoiseB
                  + organic.pink.weightC * recoilNoiseC;
        pinkCurve = 0.30f * recoilNoiseA
                  - 0.40f * recoilNoiseB
                  + 0.30f * recoilNoiseC;
    }

    // ---------- Pausa reativa ----------
    if (organic.pause.enabled &&
        (fabsf(recoilStepTargetX) > organic.pause.reactiveDriftPx ||
         fabsf(recoilStepTargetY) > organic.pause.reactiveDriftPx)) {
        recoilAccumX *= organic.pause.reactiveDamp;
        recoilAccumY *= organic.pause.reactiveDamp;
        if (random(0, 100) < (long)organic.pause.reactivePausePct) {
            recoilPauseUntil = now + random(organic.pause.minMs, organic.pause.maxMs + 1);
        }
    }

    // ---------- Pausa em curso ----------
    if ((int32_t)(now - recoilPauseUntil) < 0) {
        if (organic.jitterGauss.enabled) {
            recoilAccumX += gaussFloat(0.0f, organic.jitterGauss.scale);
            recoilAccumY += gaussFloat(0.0f, organic.jitterGauss.scale);
        }
        const int32_t jx = (int32_t)roundf(recoilAccumX);
        const int32_t jy = (int32_t)roundf(recoilAccumY);
        recoilAccumX -= (float)jx;
        recoilAccumY -= (float)jy;
        const int32_t ox = report.x, oy = report.y;
        report.x = (int8_t)constrain(ox + jx, -127, 127);
        report.y = (int8_t)constrain(oy + jy, -127, 127);
        recoilAccumX += (float)(jx - (report.x - ox));
        recoilAccumY += (float)(jy - (report.y - oy));
        return;
    }

    // ---------- Overshoot em curso ----------
    if (recoilOvershoot) {
        if ((int32_t)(now - recoilOvershootEnd) >= 0) {
            // Liquida o resíduo final: volta exatamente à posição do alvo.
            recoilAccumX -= recoilOvershootPrevX;
            recoilAccumY -= recoilOvershootPrevY;
            recoilOvershoot = false;
            recoilOvershootMagX = recoilOvershootMagY = 0.0f;
            recoilOvershootPrevX = recoilOvershootPrevY = 0.0f;
        } else {
            const float dur = (float)(recoilOvershootEnd - recoilOvershootStart);
            const float t = dur > 0.0f
                ? constrain((float)(now - recoilOvershootStart) / dur, 0.0f, 1.0f)
                : 1.0f;
            // Passa do alvo na 1a metade (seno positivo) e volta na 2a (negativo).
            const float wave = sinf(2.0f * PI * t);
            const float extraX = recoilOvershootMagX * wave;
            const float extraY = recoilOvershootMagY * wave;
            recoilAccumX += extraX - recoilOvershootPrevX;
            recoilAccumY += extraY - recoilOvershootPrevY;
            recoilOvershootPrevX = extraX;
            recoilOvershootPrevY = extraY;
        }
    }

    // ---------- Novo step ----------
    if ((int32_t)(now - recoilNextStep) >= 0 && (now - recoilStepStartTime) >= recoilStepDuration) {
        float fittsScale = 1.0f;
        if (organic.fitts.enabled) {
            const float burstAge = (now - burstStartTime) / 1000.0f;
            fittsScale = 1.0f + constrain(burstAge * organic.fitts.scale, 0.0f, organic.fitts.maxVal);
        }

        uint32_t dur;
        if (organic.lognormal.enabled) {
            dur = logNormalSample(
                organic.lognormal.stepMedianMs, organic.lognormal.stepSigma,
                organic.lognormal.stepMinMs, organic.lognormal.stepMaxMs);
        } else {
            dur = (uint32_t)organic.lognormal.stepMedianMs;
        }
        recoilStepDuration = (uint32_t)(dur * fittsScale);

        const float rateMult = 1.0f + pinkRate * organic.pink.rateMod;

        recoilStepStartX = recoilStepTargetX;
        recoilStepStartY = recoilStepTargetY;

        const float stepSec    = recoilStepDuration / 1000.0f;
        const float stepDeltaX = baseDx * rateMult * stepSec;
        const float stepDeltaY = baseDy * rateMult * stepSec;

        recoilStepTargetX = recoilStepStartX + stepDeltaX;
        recoilStepTargetY = recoilStepStartY + stepDeltaY;

        const float len = sqrtf(stepDeltaX * stepDeltaX + stepDeltaY * stepDeltaY);
        if (organic.bezier2 && len > 0.01f) {
            const float perpX = -stepDeltaY / len;
            const float perpY =  stepDeltaX / len;
            const float curve = (gaussFloat(0.0f, 1.0f) / 3.0f) * 0.30f * len
                              + pinkCurve * 0.20f * len;
            recoilBezierCtrlX = recoilStepStartX + stepDeltaX * 0.5f + perpX * curve;
            recoilBezierCtrlY = recoilStepStartY + stepDeltaY * 0.5f + perpY * curve;
        } else {
            recoilBezierCtrlX = recoilStepStartX + stepDeltaX * 0.5f;
            recoilBezierCtrlY = recoilStepStartY + stepDeltaY * 0.5f;
        }

        recoilStepStartTime = now;
        recoilNextStep      = now + recoilStepDuration;

        if (organic.pause.enabled && random(0, 100) < (long)organic.pause.pct)
            recoilPauseUntil = now + random(organic.pause.minMs, organic.pause.maxMs);

        if (organic.overshoot.enabled && !recoilOvershoot && len > 0.5f &&
            random(0, 100) < (long)organic.overshoot.pct) {
            recoilOvershoot = true;
            const float r = random(0, 1000) / 1000.0f;
            const float mag = organic.overshoot.minMag
                            + r * (organic.overshoot.maxMag - organic.overshoot.minMag);
            recoilOvershootMagX = stepDeltaX * mag;
            recoilOvershootMagY = stepDeltaY * mag;
            recoilOvershootPrevX = 0.0f;
            recoilOvershootPrevY = 0.0f;
            recoilOvershootStart = now + (uint32_t)(recoilStepDuration * (1.0f - organic.overshoot.correction));
            recoilOvershootEnd = now + recoilStepDuration;
        }
    }

    // ---------- Bézier + smoothstep ----------
    if (recoilStepDuration > 0) {
        const float tRaw = constrain(
            (float)(now - recoilStepStartTime) / (float)recoilStepDuration,
            0.0f, 1.0f);
        const float t = organic.smoothstep
            ? (tRaw * tRaw * tRaw * (10.0f - 15.0f * tRaw + 6.0f * tRaw * tRaw))
            : tRaw;
        const float mt = 1.0f - t;
        const float bzX = mt * mt * recoilStepStartX
                        + 2.0f * mt * t * recoilBezierCtrlX
                        + t * t * recoilStepTargetX;
        const float bzY = mt * mt * recoilStepStartY
                        + 2.0f * mt * t * recoilBezierCtrlY
                        + t * t * recoilStepTargetY;
        recoilAccumX += bzX - recoilPrevBezierX;
        recoilAccumY += bzY - recoilPrevBezierY;
        recoilPrevBezierX = bzX;
        recoilPrevBezierY = bzY;
    }

    // ---------- Jitter + tremor + fadiga ----------
    if (organic.jitterGauss.enabled) {
        float fatigueK = 1.0f;
        if (organic.fatigue.enabled) {
            // Saturacao exponencial da fadiga (constante de tempo = halfMs).
            const float t = (float)(now - burstStartTime) / (float)organic.fatigue.halfMs;
            fatigueK = 1.0f + organic.fatigue.maxVal * (1.0f - expf(-t));
        }
        recoilAccumX += gaussFloat(0.0f, organic.jitterGauss.scale) * fatigueK;
        recoilAccumY += gaussFloat(0.0f, organic.jitterGauss.scale) * fatigueK;
    }

    if (organic.tremor10hz.enabled) {
        const float tremorPhase = now * 0.001f * 2.0f * PI * organic.tremor10hz.hz;
        // Tremor como POSICAO (diferenca da senoide), independe do report rate.
        const float tx = sinf(tremorPhase) * organic.tremor10hz.amp;
        const float ty = cosf(tremorPhase) * organic.tremor10hz.amp;
        recoilAccumX += tx - recoilTremorPrevX;
        recoilAccumY += ty - recoilTremorPrevY;
        recoilTremorPrevX = tx;
        recoilTremorPrevY = ty;
    }

    // ---------- Aplicação final ----------
    int32_t rx = (int32_t)roundf(recoilAccumX);
    int32_t ry = (int32_t)roundf(recoilAccumY);
    recoilAccumX -= (float)rx;
    recoilAccumY -= (float)ry;

    const int32_t ox = report.x, oy = report.y;
    report.x = (int8_t)constrain(ox + rx, -127, 127);
    report.y = (int8_t)constrain(oy + ry, -127, 127);
    recoilAccumX += (float)(rx - (report.x - ox));
    recoilAccumY += (float)(ry - (report.y - oy));
}

// ============================================================
// USB Host callbacks
// ============================================================
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const*, uint16_t) {
    if (tuh_hid_interface_protocol(dev_addr, instance) != HID_ITF_PROTOCOL_MOUSE) return;
    if (tuh_hid_get_protocol(dev_addr, instance) == HID_PROTOCOL_BOOT) {
        tuh_hid_receive_report(dev_addr, instance);
        return;
    }
        // Solicita protocolo de boot (o padrão suportado por mouses HID).
        // Só encaminhamos relatórios em boot protocol (ver tuh_hid_report_received_cb).
        if (!tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_BOOT)) {
            tuh_hid_receive_report(dev_addr, instance);
        }
}
void tuh_hid_set_protocol_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t p) {
    (void)p;
    if (tuh_hid_interface_protocol(dev_addr, instance) != HID_ITF_PROTOCOL_MOUSE) return;
    // Re-arma a recepção SEMPRE. Se o boot protocol não foi aceito, os reports
    // virão em "report protocol" e serão descartados no report_received_cb
    // (suporte a report protocol exigiria ler o descritor HID do dispositivo).
    tuh_hid_receive_report(dev_addr, instance);
}
void tuh_hid_umount_cb(uint8_t, uint8_t) {
    disconnectGeneration.fetch_add(1, std::memory_order_release);
}
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len) {
    const bool boot =
        tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_MOUSE &&
        tuh_hid_get_protocol(dev_addr, instance) == HID_PROTOCOL_BOOT;
    if (boot && report && len >= 3) {
        MouseReport o{};
        o.buttons = report[0] & 0x1F;
        o.x = (int8_t)report[1];
        o.y = (int8_t)report[2];
        if (len >= 4) o.wheel = (int8_t)report[3];
        if (len >= 5) o.pan   = (int8_t)report[4];
        enqueueReport(o);
    }
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_MOUSE)
        tuh_hid_receive_report(dev_addr, instance);
}

// ============================================================
// JSON · macros de merge
// ============================================================
#define APPLY_BOOL(dst, src)  do { if (!(src).isNull()) (dst) = (bool)(src); } while(0)
#define APPLY_U32(dst, src)   do { if (!(src).isNull()) (dst) = (uint32_t)(src); } while(0)
#define APPLY_F32(dst, src)   do { if (!(src).isNull()) (dst) = (float)(src); } while(0)

static bool applyJsonConfig(JsonObjectConst config) {
    const char* active = config["activeProfile"] | "profile-a";
    JsonObjectConst profiles = config["profiles"];
    if (profiles.isNull() || !profiles.containsKey(active)) return false;
    JsonObjectConst p = profiles[active];
    if (p.isNull()) return false;

    // ---------- Fast-fire ----------
    JsonObjectConst cps = p["clicksPerSecond"];
    if (!cps.isNull()) {
        APPLY_BOOL(rapidFireEnabled, cps["enabled"]);
        const float cv = cps["cps"] | (1000.0f / targetBaseInterval);
        if (cv > 0.1f) {
            uint32_t ms = (uint32_t)roundf(1000.0f / cv);
            targetBaseInterval = constrain(ms, 50, 1000); // 1 a 20 CPS
            currentInterval = targetBaseInterval;
        }
    }

    // ---------- Recoil Control ----------
    JsonObjectConst rc = p["recoilControl"];
    if (!rc.isNull()) {
        APPLY_BOOL(recoilEnabled, rc["enabled"]);
        APPLY_F32(recoilYDown,   rc["yDown"]);
        APPLY_F32(recoilYUp,     rc["yUp"]);
        APPLY_F32(recoilXLeft,   rc["xLeft"]);
        APPLY_F32(recoilXRight,  rc["xRight"]);
        const char* t = rc["trigger"] | "secondary+primary";
        if      (strcmp(t, "secondary+primary") == 0) recoilTriggerMask = 0x03;
        else if (strcmp(t, "secondary") == 0)         recoilTriggerMask = 0x02;
        else                                            recoilTriggerMask = 0x01;
    }

    // ---------- ORGANIC ----------
    JsonObjectConst org = p["organic"];
    if (!org.isNull()) {
        JsonObjectConst ff = org;
        if (!ff["uniform"].isNull())       APPLY_BOOL(organic.fastFire.uniform,       ff["uniform"]["enabled"]);
        if (!ff["gaussian"].isNull())      APPLY_BOOL(organic.fastFire.gaussian,      ff["gaussian"]["enabled"]);
        if (!ff["randomwalk"].isNull())    APPLY_BOOL(organic.fastFire.randomwalk,    ff["randomwalk"]["enabled"]);
        if (!ff["jitterUniform"].isNull()) APPLY_BOOL(organic.fastFire.jitterUniform, ff["jitterUniform"]["enabled"]);

        JsonObjectConst r = org["reaction"];
        if (!r.isNull()) {
            APPLY_BOOL(organic.reaction.enabled,     r["enabled"]);
            APPLY_U32 (organic.reaction.minMs,       r["minMs"]);
            APPLY_U32 (organic.reaction.maxMs,       r["maxMs"]);
            APPLY_F32 (organic.reaction.medianMs,    r["medianMs"]);
            APPLY_F32 (organic.reaction.sigma,       r["sigma"]);
            APPLY_U32 (organic.reaction.distractPct, r["distractPct"]);
            APPLY_U32 (organic.reaction.distractAdd, r["distractAdd"]);
        }

        JsonObjectConst ln = org["lognormal"];
        if (!ln.isNull()) {
            APPLY_BOOL(organic.lognormal.enabled,      ln["enabled"]);
            APPLY_F32 (organic.lognormal.stepMedianMs, ln["stepMedianMs"]);
            APPLY_F32 (organic.lognormal.stepSigma,    ln["stepSigma"]);
            APPLY_U32 (organic.lognormal.stepMinMs,    ln["stepMinMs"]);
            APPLY_U32 (organic.lognormal.stepMaxMs,    ln["stepMaxMs"]);
        }

        JsonObjectConst ou = org["ornsteinUhlenbeck"];
        if (!ou.isNull()) APPLY_BOOL(organic.ornsteinUhlenbeck, ou["enabled"]);

        JsonObjectConst pk = org["pink"];
        if (!pk.isNull()) {
            APPLY_BOOL(organic.pink.enabled, pk["enabled"]);
            APPLY_U32 (organic.pink.tauAMs,  pk["tauAMs"]);
            APPLY_U32 (organic.pink.tauBMs,  pk["tauBMs"]);
            APPLY_U32 (organic.pink.tauCMs,  pk["tauCMs"]);
            APPLY_F32 (organic.pink.weightA, pk["weightA"]);
            APPLY_F32 (organic.pink.weightB, pk["weightB"]);
            APPLY_F32 (organic.pink.weightC, pk["weightC"]);
            APPLY_F32 (organic.pink.rateMod, pk["rateMod"]);
        }

        JsonObjectConst ss = org["smoothstep"];
        if (!ss.isNull()) APPLY_BOOL(organic.smoothstep, ss["enabled"]);
        JsonObjectConst bz = org["bezier2"];
        if (!bz.isNull()) APPLY_BOOL(organic.bezier2, bz["enabled"]);

        JsonObjectConst jg = org["jitterGauss"];
        if (!jg.isNull()) {
            APPLY_BOOL(organic.jitterGauss.enabled, jg["enabled"]);
            APPLY_F32 (organic.jitterGauss.scale,   jg["scale"]);
        }

        JsonObjectConst tr = org["tremor10hz"];
        if (!tr.isNull()) {
            APPLY_BOOL(organic.tremor10hz.enabled, tr["enabled"]);
            APPLY_F32 (organic.tremor10hz.hz,      tr["hz"]);
            APPLY_F32 (organic.tremor10hz.amp,     tr["amp"]);
        }

        JsonObjectConst ov = org["overshoot"];
        if (!ov.isNull()) {
            APPLY_BOOL(organic.overshoot.enabled,    ov["enabled"]);
            APPLY_U32 (organic.overshoot.pct,        ov["pct"]);
            APPLY_F32 (organic.overshoot.minMag,     ov["min"]);
            APPLY_F32 (organic.overshoot.maxMag,     ov["max"]);
            APPLY_F32 (organic.overshoot.correction, ov["correction"]);
        }

        JsonObjectConst ft = org["fitts"];
        if (!ft.isNull()) {
            APPLY_BOOL(organic.fitts.enabled, ft["enabled"]);
            APPLY_F32 (organic.fitts.scale,   ft["scale"]);
            APPLY_F32 (organic.fitts.maxVal,  ft["max"]);
        }

        JsonObjectConst fg = org["fatigue"];
        if (!fg.isNull()) {
            APPLY_BOOL(organic.fatigue.enabled, fg["enabled"]);
            APPLY_U32 (organic.fatigue.halfMs,  fg["halfMs"]);
            APPLY_F32 (organic.fatigue.maxVal,  fg["max"]);
        }

        JsonObjectConst pa = org["pause"];
        if (!pa.isNull()) {
            APPLY_BOOL(organic.pause.enabled,          pa["enabled"]);
            APPLY_U32 (organic.pause.pct,              pa["pct"]);
            APPLY_U32 (organic.pause.minMs,            pa["minMs"]);
            APPLY_U32 (organic.pause.maxMs,            pa["maxMs"]);
            APPLY_F32 (organic.pause.reactiveDriftPx,  pa["reactiveDriftPx"]);
            APPLY_U32 (organic.pause.reactivePausePct, pa["reactivePausePct"]);
            APPLY_F32 (organic.pause.reactiveDamp,     pa["reactiveDamp"]);
        }
    }

        if (organic.fatigue.halfMs < 1000) organic.fatigue.halfMs = 60000;
        if (organic.reaction.minMs > organic.reaction.maxMs) organic.reaction.minMs = organic.reaction.maxMs;
        if (organic.lognormal.stepMinMs > organic.lognormal.stepMaxMs) organic.lognormal.stepMinMs = organic.lognormal.stepMaxMs;
        if (organic.pause.minMs > organic.pause.maxMs) organic.pause.minMs = organic.pause.maxMs;
        if (organic.overshoot.minMag > organic.overshoot.maxMag) organic.overshoot.minMag = organic.overshoot.maxMag;
        if (organic.reaction.medianMs < 1.0f) organic.reaction.medianMs = 200.0f;
        if (organic.lognormal.stepMedianMs < 5.0f) organic.lognormal.stepMedianMs = 50.0f;
        if (organic.reaction.sigma < 0.0f) organic.reaction.sigma = 0.25f;
        if (organic.lognormal.stepSigma < 0.0f) organic.lognormal.stepSigma = 0.35f;
        if (organic.overshoot.correction < 0.1f) organic.overshoot.correction = 0.5f;
        if (organic.fitts.maxVal < 0.0f) organic.fitts.maxVal = 0.50f;
        if (organic.fatigue.maxVal < 0.0f) organic.fatigue.maxVal = 0.60f;
        if (organic.tremor10hz.hz < 0.1f) organic.tremor10hz.hz = 10.0f;
        if (organic.tremor10hz.amp < 0.0f) organic.tremor10hz.amp = 0.4f;

        settingsDirty = true;  // garante nova tentativa de save caso o commit falhe
        digitalWrite(LED_BUILTIN, rapidFireEnabled ? HIGH : LOW);
        saveSettings();
        return true;
    }

// ============================================================
// JSON · enviar config
// ============================================================
static void sendCurrentConfig() {
    jsonDoc.clear();
    JsonObject doc = jsonDoc.to<JsonObject>();
    doc["format"] = "pico-hid-config";
    doc["version"] = 3;
    doc["activeProfile"] = "profile-a";

    JsonObject profiles = doc.createNestedObject("profiles");
    for (const char* id : { "profile-a", "profile-b" }) {
        JsonObject prof = profiles.createNestedObject(id);

        JsonObject rc = prof.createNestedObject("recoilControl");
        rc["enabled"]     = recoilEnabled;
        rc["yDown"]       = recoilYDown;
        rc["yUp"]         = recoilYUp;
        rc["xLeft"]       = recoilXLeft;
        rc["xRight"]      = recoilXRight;
        rc["updateDelay"] = 0;
        rc["trigger"]     = (recoilTriggerMask == 0x03) ? "secondary+primary"
                          : (recoilTriggerMask == 0x02) ? "secondary" : "primary";

        JsonObject cps = prof.createNestedObject("clicksPerSecond");
        cps["enabled"]    = rapidFireEnabled;
        cps["cps"]        = 1000.0f / (float)targetBaseInterval;
        cps["advanced"]   = (targetBaseInterval < 67); // true quando > 15 CPS
        cps["intervalMs"] = targetBaseInterval;

        JsonObject org = prof.createNestedObject("organic");
        org["uniform"]["enabled"]       = organic.fastFire.uniform;
        org["gaussian"]["enabled"]      = organic.fastFire.gaussian;
        org["randomwalk"]["enabled"]    = organic.fastFire.randomwalk;
        org["jitterUniform"]["enabled"] = organic.fastFire.jitterUniform;

        JsonObject r = org.createNestedObject("reaction");
        r["enabled"]     = organic.reaction.enabled;
        r["minMs"]       = organic.reaction.minMs;
        r["maxMs"]       = organic.reaction.maxMs;
        r["medianMs"]    = organic.reaction.medianMs;
        r["sigma"]       = organic.reaction.sigma;
        r["distractPct"] = organic.reaction.distractPct;
        r["distractAdd"] = organic.reaction.distractAdd;

        JsonObject ln = org.createNestedObject("lognormal");
        ln["enabled"]      = organic.lognormal.enabled;
        ln["stepMedianMs"] = organic.lognormal.stepMedianMs;
        ln["stepSigma"]    = organic.lognormal.stepSigma;
        ln["stepMinMs"]    = organic.lognormal.stepMinMs;
        ln["stepMaxMs"]    = organic.lognormal.stepMaxMs;

        org["ornsteinUhlenbeck"]["enabled"] = organic.ornsteinUhlenbeck;

        JsonObject pk = org.createNestedObject("pink");
        pk["enabled"] = organic.pink.enabled;
        pk["tauAMs"]  = organic.pink.tauAMs;
        pk["tauBMs"]  = organic.pink.tauBMs;
        pk["tauCMs"]  = organic.pink.tauCMs;
        pk["weightA"] = organic.pink.weightA;
        pk["weightB"] = organic.pink.weightB;
        pk["weightC"] = organic.pink.weightC;
        pk["rateMod"] = organic.pink.rateMod;

        org["smoothstep"]["enabled"] = organic.smoothstep;
        org["bezier2"]["enabled"]    = organic.bezier2;

        JsonObject jg = org.createNestedObject("jitterGauss");
        jg["enabled"] = organic.jitterGauss.enabled;
        jg["scale"]   = organic.jitterGauss.scale;

        JsonObject tr = org.createNestedObject("tremor10hz");
        tr["enabled"] = organic.tremor10hz.enabled;
        tr["hz"]      = organic.tremor10hz.hz;
        tr["amp"]     = organic.tremor10hz.amp;

        JsonObject ov = org.createNestedObject("overshoot");
        ov["enabled"]    = organic.overshoot.enabled;
        ov["pct"]        = organic.overshoot.pct;
        ov["min"]        = organic.overshoot.minMag;
        ov["max"]        = organic.overshoot.maxMag;
        ov["correction"] = organic.overshoot.correction;

        JsonObject ft = org.createNestedObject("fitts");
        ft["enabled"] = organic.fitts.enabled;
        ft["scale"]   = organic.fitts.scale;
        ft["max"]     = organic.fitts.maxVal;

        JsonObject fg = org.createNestedObject("fatigue");
        fg["enabled"] = organic.fatigue.enabled;
        fg["halfMs"]  = organic.fatigue.halfMs;
        fg["max"]     = organic.fatigue.maxVal;

        JsonObject pa = org.createNestedObject("pause");
        pa["enabled"]          = organic.pause.enabled;
        pa["pct"]              = organic.pause.pct;
        pa["minMs"]            = organic.pause.minMs;
        pa["maxMs"]            = organic.pause.maxMs;
        pa["reactiveDriftPx"]  = organic.pause.reactiveDriftPx;
        pa["reactivePausePct"] = organic.pause.reactivePausePct;
        pa["reactiveDamp"]     = organic.pause.reactiveDamp;
    }

    serializeJson(jsonDoc, Serial);
    Serial.println();
}

// ============================================================
// Serial
// ============================================================
static void processSerialCommands() {
    static String buf;
    while (Serial.available()) {
        const char c = (char)Serial.read();
        if (c == '\n') {
            if (buf.length() > 0) {
                jsonDoc.clear();
                if (!deserializeJson(jsonDoc, buf) && jsonDoc.containsKey("cmd") &&
                    jsonDoc["cmd"].is<const char*>()) {
                    const char* cmd = jsonDoc["cmd"] | "";
                    if (strcmp(cmd, "set") == 0 && jsonDoc.containsKey("config") &&
                        jsonDoc["config"].is<JsonObjectConst>()) {
                        if (applyJsonConfig(jsonDoc["config"].as<JsonObjectConst>()))
                            Serial.println("{\"ok\":true,\"msg\":\"Config aplicada\"}");
                        else
                            Serial.println("{\"ok\":false,\"error\":\"Config invalida\"}");
                    } else if (strcmp(cmd, "get") == 0) {
                        sendCurrentConfig();
                    }
                }
            }
            buf = "";
        } else if (c != '\r' && buf.length() < 8192) {
            buf += c;
        }
    }
}

// ============================================================
// EEPROM
// ============================================================
#define SAVE(f) do { EEPROM.put(addr, (f)); addr += sizeof(f); } while(0)
#define LOAD(f) do { EEPROM.get(addr, (f)); addr += sizeof(f); } while(0)

static void loadSettings() {
    EEPROM.begin(EEPROM_SIZE);
    uint32_t magic = 0;
    EEPROM.get(0, magic);
    if (magic != EEPROM_MAGIC) return;

    int addr = 4;
    LOAD(rapidFireEnabled);
    LOAD(targetBaseInterval);
    LOAD(recoilEnabled);
    LOAD(recoilYDown);
    LOAD(recoilYUp);
    LOAD(recoilXLeft);
    LOAD(recoilXRight);
    LOAD(recoilTriggerMask);

    LOAD(organic.fastFire.uniform);
    LOAD(organic.fastFire.gaussian);
    LOAD(organic.fastFire.randomwalk);
    LOAD(organic.fastFire.jitterUniform);

    LOAD(organic.reaction.enabled);
    LOAD(organic.reaction.minMs);
    LOAD(organic.reaction.maxMs);
    LOAD(organic.reaction.medianMs);
    LOAD(organic.reaction.sigma);
    LOAD(organic.reaction.distractPct);
    LOAD(organic.reaction.distractAdd);

    LOAD(organic.lognormal.enabled);
    LOAD(organic.lognormal.stepMedianMs);
    LOAD(organic.lognormal.stepSigma);
    LOAD(organic.lognormal.stepMinMs);
    LOAD(organic.lognormal.stepMaxMs);

    LOAD(organic.ornsteinUhlenbeck);

    LOAD(organic.pink.enabled);
    LOAD(organic.pink.tauAMs);
    LOAD(organic.pink.tauBMs);
    LOAD(organic.pink.tauCMs);
    LOAD(organic.pink.weightA);
    LOAD(organic.pink.weightB);
    LOAD(organic.pink.weightC);
    LOAD(organic.pink.rateMod);

    LOAD(organic.smoothstep);
    LOAD(organic.bezier2);

    LOAD(organic.jitterGauss.enabled);
    LOAD(organic.jitterGauss.scale);

    LOAD(organic.tremor10hz.enabled);
    LOAD(organic.tremor10hz.hz);
    LOAD(organic.tremor10hz.amp);

    LOAD(organic.overshoot.enabled);
    LOAD(organic.overshoot.pct);
    LOAD(organic.overshoot.minMag);
    LOAD(organic.overshoot.maxMag);
    LOAD(organic.overshoot.correction);

    LOAD(organic.fitts.enabled);
    LOAD(organic.fitts.scale);
    LOAD(organic.fitts.maxVal);

    LOAD(organic.fatigue.enabled);
    LOAD(organic.fatigue.halfMs);
    LOAD(organic.fatigue.maxVal);

    LOAD(organic.pause.enabled);
    LOAD(organic.pause.pct);
    LOAD(organic.pause.minMs);
    LOAD(organic.pause.maxMs);
    LOAD(organic.pause.reactiveDriftPx);
    LOAD(organic.pause.reactivePausePct);
    LOAD(organic.pause.reactiveDamp);

    targetBaseInterval = constrain(targetBaseInterval, 50, 1000);
    if (recoilTriggerMask > 3) recoilTriggerMask = 0x03;
    if (organic.reaction.medianMs < 1.0f) organic.reaction.medianMs = 200.0f;
    if (organic.lognormal.stepMedianMs < 5.0f) organic.lognormal.stepMedianMs = 50.0f;
    if (organic.fatigue.halfMs < 1000) organic.fatigue.halfMs = 60000;
    if (organic.reaction.minMs > organic.reaction.maxMs) organic.reaction.minMs = organic.reaction.maxMs;
    if (organic.lognormal.stepMinMs > organic.lognormal.stepMaxMs) organic.lognormal.stepMinMs = organic.lognormal.stepMaxMs;
}

static void saveSettings() {
    int addr = 0;
    EEPROM.put(addr, EEPROM_MAGIC); addr += sizeof(EEPROM_MAGIC);

    SAVE(rapidFireEnabled);
    SAVE(targetBaseInterval);
    SAVE(recoilEnabled);
    SAVE(recoilYDown);
    SAVE(recoilYUp);
    SAVE(recoilXLeft);
    SAVE(recoilXRight);
    SAVE(recoilTriggerMask);

    SAVE(organic.fastFire.uniform);
    SAVE(organic.fastFire.gaussian);
    SAVE(organic.fastFire.randomwalk);
    SAVE(organic.fastFire.jitterUniform);

    SAVE(organic.reaction.enabled);
    SAVE(organic.reaction.minMs);
    SAVE(organic.reaction.maxMs);
    SAVE(organic.reaction.medianMs);
    SAVE(organic.reaction.sigma);
    SAVE(organic.reaction.distractPct);
    SAVE(organic.reaction.distractAdd);

    SAVE(organic.lognormal.enabled);
    SAVE(organic.lognormal.stepMedianMs);
    SAVE(organic.lognormal.stepSigma);
    SAVE(organic.lognormal.stepMinMs);
    SAVE(organic.lognormal.stepMaxMs);

    SAVE(organic.ornsteinUhlenbeck);

    SAVE(organic.pink.enabled);
    SAVE(organic.pink.tauAMs);
    SAVE(organic.pink.tauBMs);
    SAVE(organic.pink.tauCMs);
    SAVE(organic.pink.weightA);
    SAVE(organic.pink.weightB);
    SAVE(organic.pink.weightC);
    SAVE(organic.pink.rateMod);

    SAVE(organic.smoothstep);
    SAVE(organic.bezier2);

    SAVE(organic.jitterGauss.enabled);
    SAVE(organic.jitterGauss.scale);

    SAVE(organic.tremor10hz.enabled);
    SAVE(organic.tremor10hz.hz);
    SAVE(organic.tremor10hz.amp);

    SAVE(organic.overshoot.enabled);
    SAVE(organic.overshoot.pct);
    SAVE(organic.overshoot.minMag);
    SAVE(organic.overshoot.maxMag);
    SAVE(organic.overshoot.correction);

    SAVE(organic.fitts.enabled);
    SAVE(organic.fitts.scale);
    SAVE(organic.fitts.maxVal);

    SAVE(organic.fatigue.enabled);
    SAVE(organic.fatigue.halfMs);
    SAVE(organic.fatigue.maxVal);

    SAVE(organic.pause.enabled);
    SAVE(organic.pause.pct);
    SAVE(organic.pause.minMs);
    SAVE(organic.pause.maxMs);
    SAVE(organic.pause.reactiveDriftPx);
    SAVE(organic.pause.reactivePausePct);
    SAVE(organic.pause.reactiveDamp);

    if (EEPROM.commit()) {
        settingsDirty = false;
    }
    // se o commit falhar, settingsDirty continua true e tentamos de novo depois
}

#undef SAVE
#undef LOAD
#undef APPLY_BOOL
#undef APPLY_U32
#undef APPLY_F32

// ============================================================
// setup() · Core 0
// ============================================================
void setup() {
    // NOTA: NÃO chame set_sys_clock_khz() aqui. O clock precisa estar em 240 MHz
    // ANTES da inicialização do USB/PIO-USB, e isso só é garantido se você
    // configurar "Tools > CPU Speed: 240 MHz" no Arduino IDE (ou -DF_CPU=240000000).
    // O padrão de 125 MHz NÃO é múltiplo de 12 MHz e quebra o PIO-USB.
    watchdog_enable(1000, 1);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    loadSettings();
    currentInterval = targetBaseInterval;
    digitalWrite(LED_BUILTIN, rapidFireEnabled ? HIGH : LOW);
    if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
    usbMouse.begin();
    Serial.begin(115200);   // CDC 0 (a porta usada pelo site)
    // Se o host já enumerou antes do HID ser registrado, força re-enumeração.
    if (TinyUSBDevice.mounted()) {
        TinyUSBDevice.detach();
        delay(10);
        TinyUSBDevice.attach();
    }
    randomSeed(micros() ^ (uint32_t)analogRead(A0));
}

static void resetFastFireState() {
    isCurrentlyDown  = false;
    lastActionTime   = 0;
    biologicalOffset = 0;
    lastButtons      = 0;
    middlePressStart = 0;
    toggleTriggered  = false;
    lastLedToggle    = 0;
    ledPulseState    = false;
    digitalWrite(LED_BUILTIN, rapidFireEnabled ? HIGH : LOW);
}

// ============================================================
// loop() · Core 0
// ============================================================
void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
    TinyUSBDevice.task();
#endif
    processSerialCommands();
    const uint32_t now = millis();  // captura o tempo DEPOIS do serial

    // Overflow da fila (perda de relatório): reseta para nunca deixar botão preso.
    static uint32_t lastOverflowCount = 0;
    const uint32_t overflow = queueOverflowCount.load(std::memory_order_acquire);
    if (overflow != lastOverflowCount) {
        lastOverflowCount = overflow;
        resetFastFireState();
        requestNeutralReport();
    }

    const uint32_t gen = disconnectGeneration.load(std::memory_order_acquire);
    if (gen != observedDisconnectGeneration) {
        observedDisconnectGeneration = gen;
        requestNeutralReport();
        resetRecoilBurst();
        resetFastFireState();
    }
    if (!TinyUSBDevice.mounted()) {
        requestNeutralReport();
        hidReportInFlight = false;  // evita ficar preso após replug
        resetFastFireState();
        watchdog_update();
        return;
    }
    serviceNeutralReport();
    if (releasePending || hidReportInFlight || !usbMouse.ready()) {
        watchdog_update();
        return;
    }

    // Drena TODA a fila (0, 1 ou vários relatórios). Acumula movimento e guarda
    // o último estado dos botões/scroll para o fast-fire/recoil continuarem
    // mesmo quando o mouse para de enviar relatórios.
    MouseReport in{};
    int32_t moveX = 0, moveY = 0, moveWheel = 0, movePan = 0;
    bool hasInput = false;
    while (dequeueReport(in)) {
        hasInput = true;
        lastButtons = in.buttons;
        moveX    += in.x;
        moveY    += in.y;
        moveWheel += in.wheel;
        movePan  += in.pan;
    }

    MouseReport report{};
    report.buttons = lastButtons;
    if (hasInput) {
        report.x     = (int8_t)constrain(moveX, -127, 127);
        report.y     = (int8_t)constrain(moveY, -127, 127);
        report.wheel = (int8_t)constrain(moveWheel, -127, 127);
        report.pan   = (int8_t)constrain(movePan, -127, 127);
    }

    const uint8_t rawButtons = report.buttons;

    bool skipFrame = false;

    // Botão do meio: ajuste + toggle
    const bool middlePressed = (rawButtons & 0x04) != 0;
    const int8_t wheelScroll = report.wheel;

    if (middlePressed) {
        report.buttons &= ~0x04;

        if (wheelScroll != 0) {
            if (wheelScroll > 0) targetBaseInterval = constrain((int32_t)targetBaseInterval - 5, 50, 1000);
            else                 targetBaseInterval = constrain((int32_t)targetBaseInterval + 5, 50, 1000);
            middlePressStart = now;
            report.wheel     = 0;
            settingsDirty    = true;
        }

        if (middlePressStart == 0) middlePressStart = now;

        if (now - lastLedToggle >= (targetBaseInterval / 2)) {
            ledPulseState = !ledPulseState;
            digitalWrite(LED_BUILTIN, ledPulseState);
            lastLedToggle = now;
        }

        if (!toggleTriggered && (now - middlePressStart >= 2000)) {
            rapidFireEnabled = !rapidFireEnabled;
            requestNeutralReport();
            toggleTriggered = true;
            settingsDirty = true;
            skipFrame = true;
        }
    } else {
        if (middlePressStart != 0) {
            middlePressStart = 0;
            toggleTriggered  = false;
            digitalWrite(LED_BUILTIN, rapidFireEnabled ? HIGH : LOW);
            if (settingsDirty) saveSettings();
        }
    }

    if (skipFrame) {
        watchdog_update();
        return;
    }

    // FAST-FIRE (por tempo, usa lastButtons)
    const bool rawLeft = (rawButtons & 0x01) != 0;
    if (rapidFireEnabled && rawLeft) {
        uint8_t mb = report.buttons & ~0x01;
        if (!isCurrentlyDown && (now - lastActionTime >= currentInterval)) {
            // PRESS
            if (organic.fastFire.jitterUniform) {
                const int16_t nx = (int16_t)report.x + (int8_t)random(-1, 2);
                const int16_t ny = (int16_t)report.y + (int8_t)random(-1, 2);
                report.x = (int8_t)constrain(nx, -127, 127);
                report.y = (int8_t)constrain(ny, -127, 127);
            }
            mb |= 0x01; isCurrentlyDown = true; lastActionTime = now;
            if (organic.fastFire.gaussian) {
                currentHoldTime = (uint32_t)getGaussianRandom(14, 3);
                if (currentHoldTime < 8) currentHoldTime = 8;
            } else {
                currentHoldTime = 14;
            }
        } else if (isCurrentlyDown && (now - lastActionTime >= currentHoldTime)) {
            // RELEASE
            mb &= ~0x01; isCurrentlyDown = false; lastActionTime = now;

            if (organic.fastFire.uniform) {
                biologicalOffset += random(-4, 5);
                biologicalOffset = constrain(biologicalOffset, -20, 20);
            }

            long baseG;
            if (organic.fastFire.gaussian) {
                baseG = getGaussianRandom(targetBaseInterval, 12);
            } else {
                baseG = (long)targetBaseInterval;
            }
            const int32_t off = organic.fastFire.randomwalk ? biologicalOffset : 0;
            long next = baseG + off - (long)currentHoldTime;
            currentInterval = (uint32_t)(next < 15 ? 15 : next);
        } else if (isCurrentlyDown) {
            // HOLD
            mb |= 0x01;
            if (organic.fastFire.jitterUniform && random(0, 100) < 15) {
                const int16_t nx = (int16_t)report.x + (int8_t)random(-1, 2);
                const int16_t ny = (int16_t)report.y + (int8_t)random(-1, 2);
                report.x = (int8_t)constrain(nx, -127, 127);
                report.y = (int8_t)constrain(ny, -127, 127);
            }
        }
        report.buttons = mb;
    } else {
        isCurrentlyDown  = false;
        biologicalOffset = 0;
    }

    // RECOIL
    applyRecoilControl(report, rawButtons, now);

    hidReportInFlight = true;
    if (!usbMouse.sendReport(0, &report, sizeof(report))) hidReportInFlight = false;
    watchdog_update();
}

// ============================================================
// Core 1: USB Host
// ============================================================
void setup1() {
    pio_usb_configuration_t cfg = PIO_USB_DEFAULT_CONFIG;
    cfg.pin_dp = HOST_USB_DP_PIN;
    USBHost.configure_pio_usb(1, &cfg);
    USBHost.begin(1);
}
void loop1() { USBHost.task(); watchdog_update(); }