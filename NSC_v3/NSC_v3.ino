#include <Adafruit_NeoPixel.h> // INSTALL LIBRARY FROM LIBRARY MANAGER
#include <math.h>

#ifdef __AVR__
#include <avr/power.h>
#endif

// ============================================================
// ===================== STRIP A CONFIG ========================
// ============================================================

#define LED_PIN     A1
#define LED_COUNT   200
#define BRIGHTNESS  200

// ---------------- WHITE BLOB ----------------

#define WHITE_LENGTH_PIXELS              30
#define WHITE_STEP_SECONDS               0.005f
#define WHITE_INTENSITY                 255
#define WHITE_SPAWN_INTERVAL_SECONDS     0.7f

// ---------------- TERRACOTTA BLOB ----------------

#define TERRACOTTA_LENGTH_PIXELS              35
#define TERRACOTTA_STEP_SECONDS               0.010f
#define TERRACOTTA_INTENSITY                 255
#define TERRACOTTA_SPAWN_INTERVAL_SECONDS     0.9f

#define TERRACOTTA_RED    210
#define TERRACOTTA_GREEN   90
#define TERRACOTTA_BLUE    60

// ---------------- SHAPE / BEHAVIOUR ----------------

#define EDGE_FEATHER_PIXELS   5
#define ARRIVAL_FADE_SECONDS  0.35f

//- DO NOT EDIT ANYTHING BELOW THIS PART ----------------------------------------------------------------------------------//
//------------------------------------------------------------------------------------------------------------------------//
//------------------------------------------------------------------------------------------------------------------------//
//------------------------------------------------------------------------------------------------------------------------//
//------------------------------------------------------------------------------------------------------------------------//

// ============================================================
// ===================== STRIP B CONFIG ========================
// ============================================================

#define LED_PIN_B        A0
#define LED_COUNT_B      5
#define BRIGHTNESS_B     255

#define TRAVEL_PERIOD_MS 1200

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);


// ============================================================
// ===================== TIME CONVERSION =======================
// ============================================================

// Convert seconds → milliseconds (used internally with millis())
#define SECONDS_TO_MS(x) ((uint32_t)((x) * 1000.0f))

Adafruit_NeoPixel stripB(LED_COUNT_B, LED_PIN_B, NEO_GRB + NEO_KHZ800);

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

void clearAccumulatorsA() {
  for (int i = 0; i < LED_COUNT; i++) {
    accR[i] = accG[i] = accB[i] = accW[i] = 0;
  }
}

// ============================================================
// ===================== FEATHERING ============================
// ============================================================

uint8_t featherIntensity(int i, int length, int feather) {
  if (length <= 1) return 255;

  if (feather * 2 > length) feather = length / 2;

  int flatLength = length - 2 * feather;

  if (i < feather) {
    return 255 * (i + 1) / feather;
  }

  if (i >= feather + flatLength) {
    int t = length - i;
    return 255 * t / feather;
  }

  return 255;
}

// ============================================================
// ===================== BLOB STRUCT ===========================
// ============================================================

struct Blob {
  bool active;

  int position;
  int lengthPixels;
  int featherPixels;

  bool isWhite;

  uint8_t intensity;
  uint8_t r, g, b;

  uint32_t stepIntervalMs;
  uint32_t lastStepTimeMs;

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

void spawnWhiteBlob(uint32_t now) {
  int i = allocateBlob();
  if (i < 0) return;

  blobs[i] = {
    true,
    -WHITE_LENGTH_PIXELS,
    WHITE_LENGTH_PIXELS,
    EDGE_FEATHER_PIXELS,
    true,
    WHITE_INTENSITY,
    0,0,0,
    SECONDS_TO_MS(WHITE_STEP_SECONDS),
    now,
    now
  };
}

void spawnTerracottaBlob(uint32_t now) {
  int i = allocateBlob();
  if (i < 0) return;

  blobs[i] = {
    true,
    -TERRACOTTA_LENGTH_PIXELS,
    TERRACOTTA_LENGTH_PIXELS,
    EDGE_FEATHER_PIXELS,
    false,
    TERRACOTTA_INTENSITY,
    TERRACOTTA_RED, TERRACOTTA_GREEN, TERRACOTTA_BLUE,
    SECONDS_TO_MS(TERRACOTTA_STEP_SECONDS),
    now,
    now
  };
}

void updateBlobs(uint32_t now) {
  for (int i = 0; i < MAX_BLOBS; i++) {
    if (!blobs[i].active) continue;

    if (now - blobs[i].lastStepTimeMs >= blobs[i].stepIntervalMs) {
      blobs[i].lastStepTimeMs = now;
      blobs[i].position++;

      if (blobs[i].position >= LED_COUNT) {
        blobs[i].active = false;
      }
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

    uint32_t fadeDuration = SECONDS_TO_MS(ARRIVAL_FADE_SECONDS);

    uint8_t arrivalFactor = (fadeDuration == 0 || age >= fadeDuration)
      ? 255
      : (age * 255UL / fadeDuration);

    for (int i = 0; i < b.lengthPixels; i++) {
      int x = b.position + i;

      if (x < 0 || x >= LED_COUNT) continue;

      uint8_t featherFactor = featherIntensity(i, b.lengthPixels, b.featherPixels);
      uint8_t combinedFactor = scaleU8(featherFactor, arrivalFactor);
      uint8_t brightness = scaleU8(b.intensity, combinedFactor);

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
    strip.setPixelColor(i,
      strip.Color(accR[i], accG[i], accB[i], accW[i]));
  }
  strip.show();
}

// ============================================================
// ===================== STRIP B ===============================
// ============================================================

void updateStripB_Travel(uint32_t now) {

  float t = (float)(now % TRAVEL_PERIOD_MS) / TRAVEL_PERIOD_MS;

  for (int i = 0; i < LED_COUNT_B; i++) {

    float u = t + (float)i / LED_COUNT_B;
    u = u - floorf(u);

    float seg = u * 4.0f;
    int s = (int)seg;
    float f = seg - s;

    uint8_t r0=0,g0=0,b0=0;
    uint8_t r1=0,g1=0,b1=0;

    switch (s) {
      case 0: r1=g1=b1=WHITE_INTENSITY; break;
      case 1: r0=g0=b0=WHITE_INTENSITY; break;
      case 2: r1=TERRACOTTA_RED; g1=TERRACOTTA_GREEN; b1=TERRACOTTA_BLUE; break;
      default:r0=TERRACOTTA_RED; g0=TERRACOTTA_GREEN; b0=TERRACOTTA_BLUE; break;
    }

    stripB.setPixelColor(i,
      lerpU8(r0,r1,f),
      lerpU8(g0,g1,f),
      lerpU8(b0,b1,f)
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

  spawnWhiteBlob(now);
  spawnTerracottaBlob(now);
}

void loop() {
  uint32_t now = millis();

  if (now - lastSpawnWhiteTime >= SECONDS_TO_MS(WHITE_SPAWN_INTERVAL_SECONDS)) {
    lastSpawnWhiteTime = now;
    spawnWhiteBlob(now);
  }

  if (now - lastSpawnTerracottaTime >= SECONDS_TO_MS(TERRACOTTA_SPAWN_INTERVAL_SECONDS)) {
    lastSpawnTerracottaTime = now;
    spawnTerracottaBlob(now);
  }

  updateBlobs(now);

  clearAccumulatorsA();
  renderBlobsToAccumulators(now);
  pushToStripA();

  updateStripB_Travel(now);
}