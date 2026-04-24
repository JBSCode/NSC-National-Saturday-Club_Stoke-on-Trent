#include <Adafruit_NeoPixel.h> // INSTALL LIBRARY FROM LIBRARY MANAGER
#include <math.h>

#ifdef __AVR__
#include <avr/power.h>
#endif

// ============================================================
// ===================== USER CONFIG ===========================
// ============================================================

#define SpeedPC 20
// 0   = static
// 25  = slow
// 50  = normal slow speed
// 100 = fastest, but now 3x slower than previous version

#define SPEED_DIVIDER 4.0f
// Increase this to make everything slower.
// 3.0 = three times slower than before.

// ============================================================
// ===================== STRIP A CONFIG ========================
// ============================================================

#define LED_PIN     A1
#define LED_COUNT   200
#define BRIGHTNESS  200

// ---------------- WHITE BLOB ----------------

#define WHITE_LENGTH_PIXELS   30
#define WHITE_INTENSITY       255

// ---------------- TERRACOTTA BLOB ----------------

#define TERRACOTTA_LENGTH_PIXELS   35
#define TERRACOTTA_INTENSITY       255

#define TERRACOTTA_RED    210
#define TERRACOTTA_GREEN   90
#define TERRACOTTA_BLUE    60

// ---------------- SHAPE / BEHAVIOUR ----------------

#define EDGE_FEATHER_PIXELS   8
#define ARRIVAL_FADE_SECONDS  0.35f

// ============================================================
// ===================== STRIP B CONFIG ========================
// ============================================================

#define LED_PIN_B        A0
#define LED_COUNT_B      5
#define BRIGHTNESS_B     255

// ============================================================
// ========== INTERNAL ORIGINAL SPEED SETTINGS =================
// ============================================================

#define WHITE_PIXELS_PER_SECOND_BASE        200.0f
#define TERRACOTTA_PIXELS_PER_SECOND_BASE   100.0f

#define WHITE_SPAWN_INTERVAL_SECONDS_BASE       0.7f
#define TERRACOTTA_SPAWN_INTERVAL_SECONDS_BASE  0.9f

#define TRAVEL_PERIOD_MS_BASE                   1200

//- DO NOT EDIT ANYTHING BELOW THIS PART ----------------------------------------------------------------------------------//
//------------------------------------------------------------------------------------------------------------------------//
//------------------------------------------------------------------------------------------------------------------------//
//------------------------------------------------------------------------------------------------------------------------//
//------------------------------------------------------------------------------------------------------------------------//

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripB(LED_COUNT_B, LED_PIN_B, NEO_GRB + NEO_KHZ800);

// ============================================================
// ===================== SPEED HELPERS =========================
// ============================================================

int clampedSpeedPC() {
  if (SpeedPC < 0) return 0;
  if (SpeedPC > 100) return 100;
  return SpeedPC;
}

float speedScale() {
  if (clampedSpeedPC() <= 0) return 0.0f;

  // Before:
  // SpeedPC 50 = original/full speed
  // SpeedPC 100 = double speed
  //
  // Now:
  // The whole scale is divided by SPEED_DIVIDER.
  return (clampedSpeedPC() / 50.0f) / SPEED_DIVIDER;
}

bool animationEnabled() {
  return clampedSpeedPC() > 0;
}

uint32_t scaledSecondsToMs(float secondsAtFullSpeed) {
  float s = speedScale();

  if (s <= 0.0f) {
    return 0xFFFFFFFFUL;
  }

  return (uint32_t)((secondsAtFullSpeed * 1000.0f) / s);
}

uint32_t scaledTravelPeriodMs() {
  float s = speedScale();

  if (s <= 0.0f) {
    return 0xFFFFFFFFUL;
  }

  return (uint32_t)(TRAVEL_PERIOD_MS_BASE / s);
}

// ============================================================
// ===================== ACCUMULATORS ==========================
// ============================================================

uint8_t accR[LED_COUNT];
uint8_t accG[LED_COUNT];
uint8_t accB[LED_COUNT];
uint8_t accW[LED_COUNT];

// ============================================================
// ===================== UTILITY FUNCTIONS =====================
// ============================================================

static inline uint8_t addClampU8(uint8_t a, uint8_t b) {
  uint16_t s = a + b;
  return (s > 255) ? 255 : s;
}

static inline uint8_t scaleU8(uint8_t v, uint8_t scale) {
  return (v * scale + 127) / 255;
}

static inline uint8_t lerpU8(uint8_t a, uint8_t b, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  return a + (b - a) * t + 0.5f;
}

static inline uint8_t scaleFloatToU8(uint8_t v, float scale) {
  if (scale < 0.0f) scale = 0.0f;
  if (scale > 1.0f) scale = 1.0f;

  return (uint8_t)(v * scale + 0.5f);
}

void clearAccumulatorsA() {
  for (int i = 0; i < LED_COUNT; i++) {
    accR[i] = accG[i] = accB[i] = accW[i] = 0;
  }
}

// ============================================================
// ===================== FEATHERING ============================
// ============================================================

float featherIntensityFloat(float i, float length, float feather) {
  if (length <= 1.0f) return 1.0f;

  if (feather * 2.0f > length) {
    feather = length * 0.5f;
  }

  float flatLength = length - 2.0f * feather;

  if (i < feather) {
    return (i + 1.0f) / feather;
  }

  if (i >= feather + flatLength) {
    float t = length - i;
    return t / feather;
  }

  return 1.0f;
}

// ============================================================
// ===================== BLOB STRUCT ===========================
// ============================================================

struct Blob {
  bool active;

  float position;
  float velocityPixelsPerSecond;

  int lengthPixels;
  int featherPixels;

  bool isWhite;

  uint8_t intensity;
  uint8_t r, g, b;

  uint32_t lastUpdateTimeMs;
  uint32_t spawnTimeMs;
};

#define MAX_BLOBS 16
Blob blobs[MAX_BLOBS];

// ============================================================
// ===================== BLOB MANAGEMENT =======================
// ============================================================

int allocateBlob() {
  for (int i = 0; i < MAX_BLOBS; i++) {
    if (!blobs[i].active) return i;
  }

  return -1;
}

void spawnWhiteBlob(uint32_t now, float startPosition) {
  int i = allocateBlob();
  if (i < 0) return;

  blobs[i] = {
    true,
    startPosition,
    WHITE_PIXELS_PER_SECOND_BASE * speedScale(),
    WHITE_LENGTH_PIXELS,
    EDGE_FEATHER_PIXELS,
    true,
    WHITE_INTENSITY,
    0, 0, 0,
    now,
    now
  };
}

void spawnTerracottaBlob(uint32_t now, float startPosition) {
  int i = allocateBlob();
  if (i < 0) return;

  blobs[i] = {
    true,
    startPosition,
    TERRACOTTA_PIXELS_PER_SECOND_BASE * speedScale(),
    TERRACOTTA_LENGTH_PIXELS,
    EDGE_FEATHER_PIXELS,
    false,
    TERRACOTTA_INTENSITY,
    TERRACOTTA_RED, TERRACOTTA_GREEN, TERRACOTTA_BLUE,
    now,
    now
  };
}

void updateBlobs(uint32_t now) {
  if (!animationEnabled()) return;

  for (int i = 0; i < MAX_BLOBS; i++) {
    if (!blobs[i].active) continue;

    float dt = (now - blobs[i].lastUpdateTimeMs) / 1000.0f;
    blobs[i].lastUpdateTimeMs = now;

    blobs[i].velocityPixelsPerSecond = blobs[i].isWhite
      ? WHITE_PIXELS_PER_SECOND_BASE * speedScale()
      : TERRACOTTA_PIXELS_PER_SECOND_BASE * speedScale();

    blobs[i].position += blobs[i].velocityPixelsPerSecond * dt;

    if (blobs[i].position >= LED_COUNT) {
      blobs[i].active = false;
    }
  }
}

// ============================================================
// ===================== RENDERING =============================
// ============================================================

void renderBlobsToAccumulators(uint32_t now) {
  for (int bi = 0; bi < MAX_BLOBS; bi++) {
    if (!blobs[bi].active) continue;

    Blob &b = blobs[bi];

    uint32_t age = now - b.spawnTimeMs;
    uint32_t fadeDuration = (uint32_t)(ARRIVAL_FADE_SECONDS * 1000.0f);

    float arrivalFactor = 1.0f;

    if (fadeDuration > 0 && age < fadeDuration) {
      arrivalFactor = (float)age / (float)fadeDuration;
    }

    int renderStart = floor(b.position) - 1;
    int renderEnd = ceil(b.position + b.lengthPixels) + 1;

    for (int x = renderStart; x <= renderEnd; x++) {
      if (x < 0 || x >= LED_COUNT) continue;

      float local = (float)x - b.position;

      if (local < 0.0f || local >= b.lengthPixels) continue;

      float featherFactor = featherIntensityFloat(local, b.lengthPixels, b.featherPixels);

      float combinedFactor = featherFactor * arrivalFactor;
      uint8_t brightness = scaleFloatToU8(b.intensity, combinedFactor);

      if (b.isWhite) {
        accW[x] = addClampU8(accW[x], brightness);
      } else {
        accR[x] = addClampU8(accR[x], scaleU8(b.r, brightness));
        accG[x] = addClampU8(accG[x], scaleU8(b.g, brightness));
        accB[x] = addClampU8(accB[x], scaleU8(b.b, brightness));
      }
    }
  }
}

void pushToStripA() {
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(
      i,
      strip.Color(accR[i], accG[i], accB[i], accW[i])
    );
  }

  strip.show();
}

// ============================================================
// ===================== STRIP B ===============================
// ============================================================

void updateStripB_Travel(uint32_t now) {
  uint32_t period = scaledTravelPeriodMs();

  float t = 0.0f;

  if (animationEnabled()) {
    t = (float)(now % period) / period;
  }

  for (int i = 0; i < LED_COUNT_B; i++) {
    float u = t + (float)i / LED_COUNT_B;
    u = u - floorf(u);

    float seg = u * 4.0f;
    int s = (int)seg;
    float f = seg - s;

    uint8_t r0 = 0, g0 = 0, b0 = 0;
    uint8_t r1 = 0, g1 = 0, b1 = 0;

    switch (s) {
      case 0:
        r1 = g1 = b1 = WHITE_INTENSITY;
        break;

      case 1:
        r0 = g0 = b0 = WHITE_INTENSITY;
        break;

      case 2:
        r1 = TERRACOTTA_RED;
        g1 = TERRACOTTA_GREEN;
        b1 = TERRACOTTA_BLUE;
        break;

      default:
        r0 = TERRACOTTA_RED;
        g0 = TERRACOTTA_GREEN;
        b0 = TERRACOTTA_BLUE;
        break;
    }

    stripB.setPixelColor(
      i,
      lerpU8(r0, r1, f),
      lerpU8(g0, g1, f),
      lerpU8(b0, b1, f)
    );
  }

  stripB.show();
}

// ============================================================
// ===================== MAIN LOOP =============================
// ============================================================

uint32_t lastSpawnWhiteTime = 0;
uint32_t lastSpawnTerracottaTime = 0;

void setup() {
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show();

  stripB.begin();
  stripB.setBrightness(BRIGHTNESS_B);
  stripB.show();

  uint32_t now = millis();

  lastSpawnWhiteTime = now;
  lastSpawnTerracottaTime = now;

  if (animationEnabled()) {
    spawnWhiteBlob(now, -WHITE_LENGTH_PIXELS);
    spawnTerracottaBlob(now, -TERRACOTTA_LENGTH_PIXELS);
  } else {
    // Static mode: visible, frozen blobs.
    spawnWhiteBlob(now, 0);
    spawnTerracottaBlob(now, LED_COUNT / 2);
  }
}

void loop() {
  uint32_t now = millis();

  if (animationEnabled()) {
    if (now - lastSpawnWhiteTime >= scaledSecondsToMs(WHITE_SPAWN_INTERVAL_SECONDS_BASE)) {
      lastSpawnWhiteTime = now;
      spawnWhiteBlob(now, -WHITE_LENGTH_PIXELS);
    }

    if (now - lastSpawnTerracottaTime >= scaledSecondsToMs(TERRACOTTA_SPAWN_INTERVAL_SECONDS_BASE)) {
      lastSpawnTerracottaTime = now;
      spawnTerracottaBlob(now, -TERRACOTTA_LENGTH_PIXELS);
    }
  }

  updateBlobs(now);

  clearAccumulatorsA();
  renderBlobsToAccumulators(now);
  pushToStripA();

  updateStripB_Travel(now);
}