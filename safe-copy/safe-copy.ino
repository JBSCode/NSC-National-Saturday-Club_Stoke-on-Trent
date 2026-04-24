// Strip A: spawn new blobs at input (pixel 0) regularly, arrive smoothly (spawn off-strip + fade-in)
// Strip B: unchanged travelling gradient on 5px RGB strip.

#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
 #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif
#include <math.h>

// ===================== STRIP A (PIN 0) =====================
#define LED_PIN     A1
#define LED_COUNT   200
#define BRIGHTNESS  200

#define WHITE_LEN          30
#define WHITE_STEP_MS      5
#define WHITE_LEVEL        255
#define WHITE_SPAWN_MS     700

#define TERRA_LEN          35
#define TERRA_STEP_MS      10
#define TERRA_LEVEL        255
#define TERRA_SPAWN_MS     900

#define TERRA_R            210
#define TERRA_G            90
#define TERRA_B            60

#define FEATHER_LEN        5

// NEW: arrival fade time (ms) so blobs don't "snap" on when they first appear
#define ARRIVE_FADE_MS     350

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ===================== STRIP B (PIN 1, RGB ONLY) =====================
#define LED_PIN_B        A0
#define LED_COUNT_B      5
#define BRIGHTNESS_B     255
#define TRAVEL_PERIOD_MS 1200
#define BLOB_WIDTH       2.2f

Adafruit_NeoPixel stripB(LED_COUNT_B, LED_PIN_B, NEO_GRB + NEO_KHZ800);

// -------- Accumulators for strip A --------
uint8_t accR[LED_COUNT];
uint8_t accG[LED_COUNT];
uint8_t accB[LED_COUNT];
uint8_t accW[LED_COUNT];

// -------- Helpers --------
static inline uint8_t addClampU8(uint8_t a, uint8_t b) {
  uint16_t s = (uint16_t)a + (uint16_t)b;
  return (s > 255) ? 255 : (uint8_t)s;
}

static inline uint8_t scaleU8(uint8_t v, uint8_t scale /*0..255*/) {
  return (uint8_t)(((uint16_t)v * (uint16_t)scale + 127) / 255);
}

static inline uint8_t lerpU8(uint8_t a, uint8_t b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return (uint8_t)(a + (b - a) * t + 0.5f);
}

void clearAccumulatorsA() {
  for (int i = 0; i < LED_COUNT; i++) {
    accR[i] = accG[i] = accB[i] = accW[i] = 0;
  }
}

// Feather intensity 0..255 for pixel i in a feathered block of length len
static inline uint8_t featherIntensity(int i, int len, int feather) {
  if (len <= 1) return 255;

  if (feather < 0) feather = 0;
  if (feather * 2 > len) feather = len / 2;

  int flatLen = len - 2 * feather;
  if (feather == 0) return 255;

  if (i < feather) {
    return (uint8_t)((uint32_t)255 * (uint32_t)(i + 1) / (uint32_t)feather);
  }

  if (i >= feather + flatLen) {
    int t = len - i; // 1..feather
    return (uint8_t)((uint32_t)255 * (uint32_t)t / (uint32_t)feather);
  }

  return 255;
}

// -------- Spawned blob system (Strip A) --------
struct Blob {
  bool active;
  int pos;               // head position (can be negative while arriving)
  int len;
  int feather;
  bool isWhite;          // true=W blob, false=RGB blob
  uint8_t level;         // WHITE_LEVEL or TERRA_LEVEL
  uint8_t r, g, b;       // used if !isWhite
  uint32_t stepMs;
  uint32_t lastStepMs;
  uint32_t spawnMs;      // NEW: for arrival fade
};

#define MAX_BLOBS 16
Blob blobs[MAX_BLOBS];

int allocBlob() {
  for (int i = 0; i < MAX_BLOBS; i++) {
    if (!blobs[i].active) return i;
  }
  return -1;
}

void spawnWhiteBlob(uint32_t now) {
  int idx = allocBlob();
  if (idx < 0) return;

  blobs[idx].active = true;
  blobs[idx].len = WHITE_LEN;
  blobs[idx].pos = -blobs[idx].len;         // NEW: start off-strip so it "enters"
  blobs[idx].feather = FEATHER_LEN;
  blobs[idx].isWhite = true;
  blobs[idx].level = WHITE_LEVEL;
  blobs[idx].r = blobs[idx].g = blobs[idx].b = 0;
  blobs[idx].stepMs = WHITE_STEP_MS;
  blobs[idx].lastStepMs = now;
  blobs[idx].spawnMs = now;                 // NEW
}

void spawnTerraBlob(uint32_t now) {
  int idx = allocBlob();
  if (idx < 0) return;

  blobs[idx].active = true;
  blobs[idx].len = TERRA_LEN;
  blobs[idx].pos = -blobs[idx].len;         // NEW: start off-strip so it "enters"
  blobs[idx].feather = FEATHER_LEN;
  blobs[idx].isWhite = false;
  blobs[idx].level = TERRA_LEVEL;
  blobs[idx].r = TERRA_R;
  blobs[idx].g = TERRA_G;
  blobs[idx].b = TERRA_B;
  blobs[idx].stepMs = TERRA_STEP_MS;
  blobs[idx].lastStepMs = now;
  blobs[idx].spawnMs = now;                 // NEW
}

void updateBlobs(uint32_t now) {
  for (int i = 0; i < MAX_BLOBS; i++) {
    if (!blobs[i].active) continue;

    if ((uint32_t)(now - blobs[i].lastStepMs) >= blobs[i].stepMs) {
      blobs[i].lastStepMs = now;
      blobs[i].pos++;

      // Kill once the whole blob has left the strip
      if (blobs[i].pos >= LED_COUNT) {
        blobs[i].active = false;
      }
    }
  }
}

void renderBlobsToAccumulators(uint32_t now) {
  for (int bi = 0; bi < MAX_BLOBS; bi++) {
    if (!blobs[bi].active) continue;
    Blob &b = blobs[bi];

    int len = b.len;
    if (len < 1) continue;
    if (len > LED_COUNT) len = LED_COUNT;

    // NEW: arrival fade factor 0..255
    uint32_t age = (uint32_t)(now - b.spawnMs);
    uint8_t arriveK = (ARRIVE_FADE_MS == 0) ? 255
                    : (age >= ARRIVE_FADE_MS ? 255 : (uint8_t)((age * 255UL) / (uint32_t)ARRIVE_FADE_MS));

    for (int i = 0; i < len; i++) {
      int x = b.pos + i;                 // absolute LED index along strip
      if (x < 0 || x >= LED_COUNT) continue;

      uint8_t kFeather = featherIntensity(i, len, b.feather);   // 0..255
      uint8_t k = scaleU8(kFeather, arriveK);                   // combine feather + arrival fade
      uint8_t a = scaleU8(b.level, k);                          // 0..level

      if (b.isWhite) {
        accW[x] = addClampU8(accW[x], a);
      } else {
        uint8_t r = scaleU8(b.r, a);
        uint8_t g = scaleU8(b.g, a);
        uint8_t bl = scaleU8(b.b, a);
        accR[x] = addClampU8(accR[x], r);
        accG[x] = addClampU8(accG[x], g);
        accB[x] = addClampU8(accB[x], bl);
      }
    }
  }
}

void pushToStripA() {
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, strip.Color(accR[i], accG[i], accB[i], accW[i]));
  }
  strip.show();
}

// -------- Strip B travelling gradient (unchanged) --------
static inline float bump(float dist, float width) {
  if (width <= 0.0001f) return (dist < 0.5f) ? 1.0f : 0.0f;
  float x = dist / width;
  if (x >= 1.0f) return 0.0f;
  return 0.5f + 0.5f * cosf(x * 3.1415926f);
}

// Replace your updateStripB_Travel() with this version:

void updateStripB_Travel(uint32_t now) {
  // phase moves forward over time (0..1)
  float t = (float)(now % TRAVEL_PERIOD_MS) / (float)TRAVEL_PERIOD_MS;

  // How quickly the pattern varies across the 5 LEDs:
  // 1.0 = one full cycle across the strip
  const float cyclesAcrossStrip = 1.0f;

  uint8_t whiteRGB = (uint8_t)WHITE_LEVEL; // keep your knob

  for (int i = 0; i < LED_COUNT_B; i++) {
    // phase per pixel (wrap 0..1)
    float u = t + cyclesAcrossStrip * ((float)i / (float)LED_COUNT_B);
    u = u - floorf(u); // frac

    // Map u (0..1) into 4 segments (0..0.25..0.5..0.75..1)
    // Segment 0: black -> white
    // Segment 1: white -> black
    // Segment 2: black -> terracotta
    // Segment 3: terracotta -> black
    float seg = u * 4.0f;           // 0..4
    int s = (int)seg;               // 0..3
    float f = seg - (float)s;       // 0..1 within segment

    uint8_t r0=0,g0=0,b0=0;
    uint8_t r1=0,g1=0,b1=0;

    switch (s) {
      case 0: // black -> white
        r0 = 0;        g0 = 0;        b0 = 0;
        r1 = whiteRGB; g1 = whiteRGB; b1 = whiteRGB;
        break;

      case 1: // white -> black
        r0 = whiteRGB; g0 = whiteRGB; b0 = whiteRGB;
        r1 = 0;        g1 = 0;        b1 = 0;
        break;

      case 2: // black -> terracotta
        r0 = 0;      g0 = 0;      b0 = 0;
        r1 = TERRA_R; g1 = TERRA_G; b1 = TERRA_B;
        break;

      default: // 3: terracotta -> black
        r0 = TERRA_R; g0 = TERRA_G; b0 = TERRA_B;
        r1 = 0;       g1 = 0;       b1 = 0;
        break;
    }

    uint8_t r = lerpU8(r0, r1, f);
    uint8_t g = lerpU8(g0, g1, f);
    uint8_t b = lerpU8(b0, b1, f);

    stripB.setPixelColor(i, r, g, b);
  }

  stripB.show();
}

// -------- Spawners --------
uint32_t lastSpawnWhite = 0;
uint32_t lastSpawnTerra = 0;

void setup() {
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.show();

  stripB.begin();
  stripB.setBrightness(BRIGHTNESS_B);
  stripB.show();

  for (int i = 0; i < MAX_BLOBS; i++) blobs[i].active = false;

  uint32_t now = millis();
  lastSpawnWhite = now;
  lastSpawnTerra = now;

  // Optional: start with one arriving immediately
  spawnWhiteBlob(now);
  spawnTerraBlob(now);
}

void loop() {
  uint32_t now = millis();

  // Spawn new patterns at the input regularly
  if ((uint32_t)(now - lastSpawnWhite) >= WHITE_SPAWN_MS) {
    lastSpawnWhite = now;
    spawnWhiteBlob(now);
  }
  if ((uint32_t)(now - lastSpawnTerra) >= TERRA_SPAWN_MS) {
    lastSpawnTerra = now;
    spawnTerraBlob(now);
  }

  // Move blobs
  updateBlobs(now);

  // Render strip A
  clearAccumulatorsA();
  renderBlobsToAccumulators(now);
  pushToStripA();

  // Strip B
  updateStripB_Travel(now);
}