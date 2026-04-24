// Strip A: smooth sub-pixel blobs entering from pixel 0
// Strip B: travelling gradient on 20px RGB strip.
// RGB strips only. No RGBW / W channel.

#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
 #include <avr/power.h>
#endif
#include <math.h>

// ===================== STRIP A =====================
#define LED_PIN     A1
#define LED_COUNT   200
#define BRIGHTNESS  200

#define WHITE_LEN          30
#define WHITE_STEP_MS      70
#define WHITE_LEVEL        128
#define WHITE_SPAWN_MS     4500

#define TERRA_LEN          35
#define TERRA_STEP_MS      100
#define TERRA_LEVEL        255
#define TERRA_SPAWN_MS     2000

#define TERRA_R            210
#define TERRA_G            90
#define TERRA_B            60

#define FEATHER_LEN        5
#define ARRIVE_FADE_MS     350

// Fixed-point precision.
// 1 LED = 256 sub-steps.
// This removes the low-speed LED-to-LED stepping.
#define FP 256L

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ===================== STRIP B =====================
#define LED_PIN_B        A0
#define LED_COUNT_B      20
#define BRIGHTNESS_B     255
#define TRAVEL_PERIOD_MS 4000
#define BLOB_WIDTH       2.2f

Adafruit_NeoPixel stripB(LED_COUNT_B, LED_PIN_B, NEO_GRB + NEO_KHZ800);

// -------- Accumulators for strip A --------
uint8_t accR[LED_COUNT];
uint8_t accG[LED_COUNT];
uint8_t accB[LED_COUNT];

// -------- Helpers --------
static inline uint8_t addClampU8(uint8_t a, uint8_t b) {
  uint16_t s = (uint16_t)a + (uint16_t)b;
  return (s > 255) ? 255 : (uint8_t)s;
}

static inline uint8_t scaleU8(uint8_t v, uint8_t scale) {
  return (uint8_t)(((uint16_t)v * (uint16_t)scale + 127) / 255);
}

static inline uint8_t lerpU8(uint8_t a, uint8_t b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return (uint8_t)(a + (b - a) * t + 0.5f);
}

void clearAccumulatorsA() {
  for (int i = 0; i < LED_COUNT; i++) {
    accR[i] = accG[i] = accB[i] = 0;
  }
}

// Fixed-point feather intensity.
// local is in FP units from start of blob.
// len and feather are in LEDs.
static inline uint8_t featherIntensityFP(long local, int len, int feather) {
  if (len <= 1) return 255;

  if (feather < 0) feather = 0;
  if (feather * 2 > len) feather = len / 2;
  if (feather == 0) return 255;

  long lenFP = (long)len * FP;
  long featherFP = (long)feather * FP;

  if (local < 0 || local >= lenFP) return 0;

  if (local < featherFP) {
    return (uint8_t)((local * 255L) / featherFP);
  }

  if (local >= lenFP - featherFP) {
    long t = lenFP - local;
    return (uint8_t)((t * 255L) / featherFP);
  }

  return 255;
}

// -------- Spawned blob system: Strip A --------
struct Blob {
  bool active;
  long pos;              // fixed-point start position: LED index * FP
  int len;
  int feather;
  bool isWhite;
  uint8_t level;
  uint8_t r, g, b;
  uint32_t stepMs;       // time to move 1 whole LED
  uint32_t lastStepMs;
  uint32_t spawnMs;
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
  blobs[idx].pos = -(long)WHITE_LEN * FP;
  blobs[idx].feather = FEATHER_LEN;
  blobs[idx].isWhite = true;
  blobs[idx].level = WHITE_LEVEL;
  blobs[idx].r = WHITE_LEVEL;
  blobs[idx].g = WHITE_LEVEL;
  blobs[idx].b = WHITE_LEVEL;
  blobs[idx].stepMs = WHITE_STEP_MS;
  blobs[idx].lastStepMs = now;
  blobs[idx].spawnMs = now;
}

void spawnTerraBlob(uint32_t now) {
  int idx = allocBlob();
  if (idx < 0) return;

  blobs[idx].active = true;
  blobs[idx].len = TERRA_LEN;
  blobs[idx].pos = -(long)TERRA_LEN * FP;
  blobs[idx].feather = FEATHER_LEN;
  blobs[idx].isWhite = false;
  blobs[idx].level = TERRA_LEVEL;
  blobs[idx].r = TERRA_R;
  blobs[idx].g = TERRA_G;
  blobs[idx].b = TERRA_B;
  blobs[idx].stepMs = TERRA_STEP_MS;
  blobs[idx].lastStepMs = now;
  blobs[idx].spawnMs = now;
}

void updateBlobs(uint32_t now) {
  for (int i = 0; i < MAX_BLOBS; i++) {
    if (!blobs[i].active) continue;

    uint32_t elapsed = now - blobs[i].lastStepMs;
    if (elapsed == 0) continue;

    blobs[i].lastStepMs = now;

    // Same speed meaning as before:
    // stepMs = milliseconds to move 1 LED.
    // Now movement is sub-pixel smooth.
    blobs[i].pos += ((long)elapsed * FP) / blobs[i].stepMs;

    // Kill once the whole blob start has passed the strip end.
    if (blobs[i].pos >= (long)LED_COUNT * FP) {
      blobs[i].active = false;
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

    uint32_t age = now - b.spawnMs;
    uint8_t arriveK = (ARRIVE_FADE_MS == 0) ? 255 :
      (age >= ARRIVE_FADE_MS ? 255 : (uint8_t)((age * 255UL) / ARRIVE_FADE_MS));

    int startX = b.pos / FP;
    int endX = (b.pos + ((long)len * FP)) / FP + 1;

    if (startX < 0) startX = 0;
    if (endX > LED_COUNT) endX = LED_COUNT;

    for (int x = startX; x < endX; x++) {
      long pixelPos = (long)x * FP;
      long local = pixelPos - b.pos;

      uint8_t kFeather = featherIntensityFP(local, len, b.feather);
      if (kFeather == 0) continue;

      uint8_t k = scaleU8(kFeather, arriveK);
      uint8_t a = scaleU8(b.level, k);

      uint8_t r = scaleU8(b.r, a);
      uint8_t g = scaleU8(b.g, a);
      uint8_t bl = scaleU8(b.b, a);

      accR[x] = addClampU8(accR[x], r);
      accG[x] = addClampU8(accG[x], g);
      accB[x] = addClampU8(accB[x], bl);
    }
  }
}

void pushToStripA() {
  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, strip.Color(accR[i], accG[i], accB[i]));
  }
  strip.show();
}

// -------- Strip B travelling gradient --------
void updateStripB_Travel(uint32_t now) {
  float t = (float)(now % TRAVEL_PERIOD_MS) / (float)TRAVEL_PERIOD_MS;
  const float cyclesAcrossStrip = 1.0f;

  uint8_t whiteRGB = (uint8_t)WHITE_LEVEL;

  for (int i = 0; i < LED_COUNT_B; i++) {
    float u = t + cyclesAcrossStrip * ((float)i / (float)LED_COUNT_B);
    u = u - floorf(u);

    float seg = u * 4.0f;
    int s = (int)seg;
    float f = seg - (float)s;

    uint8_t r0 = 0, g0 = 0, b0 = 0;
    uint8_t r1 = 0, g1 = 0, b1 = 0;

    switch (s) {
      case 0:
        r1 = whiteRGB;
        g1 = whiteRGB;
        b1 = whiteRGB;
        break;

      case 1:
        r0 = whiteRGB;
        g0 = whiteRGB;
        b0 = whiteRGB;
        break;

      case 2:
        r1 = TERRA_R;
        g1 = TERRA_G;
        b1 = TERRA_B;
        break;

      default:
        r0 = TERRA_R;
        g0 = TERRA_G;
        b0 = TERRA_B;
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

  for (int i = 0; i < MAX_BLOBS; i++) {
    blobs[i].active = false;
  }

  uint32_t now = millis();
  lastSpawnWhite = now;
  lastSpawnTerra = now;

  spawnWhiteBlob(now);
  spawnTerraBlob(now);
}

void loop() {
  uint32_t now = millis();

  if ((uint32_t)(now - lastSpawnWhite) >= WHITE_SPAWN_MS) {
    lastSpawnWhite = now;
    spawnWhiteBlob(now);
  }

  if ((uint32_t)(now - lastSpawnTerra) >= TERRA_SPAWN_MS) {
    lastSpawnTerra = now;
    spawnTerraBlob(now);
  }

  updateBlobs(now);

  clearAccumulatorsA();
  renderBlobsToAccumulators(now);
  pushToStripA();

  updateStripB_Travel(now);
}