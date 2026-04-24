#include <Adafruit_NeoPixel.h>
#include <math.h>

// ===================== CONFIG =====================
int speedPct = 10; // Try 5-19 for ultra-slow "liquid" motion

#define LED_PIN_A    A1
#define LED_COUNT_A  200
#define LED_PIN_B    A0
#define LED_COUNT_B  20

// RGBW for A, RGB for B.
Adafruit_NeoPixel stripA(LED_COUNT_A, LED_PIN_A, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripB(LED_COUNT_B, LED_PIN_B, NEO_GRB + NEO_KHZ800);

#define WHITE_LEN 30
#define WHITE_STEP_MS 5
#define WHITE_SPAWN_MS 700
#define TERRA_LEN 35
#define TERRA_STEP_MS 10
#define TERRA_SPAWN_MS 900
#define TERRA_R 210
#define TERRA_G 90
#define TERRA_B 60
#define ARRIVE_FADE_MS 350

struct Blob {
  float pos;
  float velocity; 
  uint32_t spawnMs;
  uint8_t len;
  uint8_t r, g, b, w;
  bool active;
};

#define MAX_BLOBS 12 
Blob blobs[MAX_BLOBS];

// -------- Helpers --------
static inline uint8_t scaleU8(uint8_t v, uint8_t scale) {
  return (uint8_t)(((uint16_t)v * scale) >> 8);
}

static inline uint8_t addClamp(uint8_t a, uint8_t b) {
  uint16_t sum = (uint16_t)a + b;
  return (sum > 255) ? 255 : (uint8_t)sum;
}

// -------- Logic --------

void spawn(bool isWhite, uint32_t now) {
  for (int i = 0; i < MAX_BLOBS; i++) {
    if (!blobs[i].active) {
      blobs[i].active = true;
      blobs[i].len = isWhite ? WHITE_LEN : TERRA_LEN;
      blobs[i].pos = -(float)blobs[i].len;
      blobs[i].spawnMs = now;
      blobs[i].velocity = 1.0f / (isWhite ? WHITE_STEP_MS : TERRA_STEP_MS);
      if (isWhite) { blobs[i].r=0; blobs[i].g=0; blobs[i].b=0; blobs[i].w=255; }
      else { blobs[i].r=TERRA_R; blobs[i].g=TERRA_G; blobs[i].b=TERRA_B; blobs[i].w=0; }
      return;
    }
  }
}

void render(uint32_t now, float dt) {
  float speedF = speedPct / 500.0f; // 5x slowdown divisor
  
  stripA.clear(); 

  for (int b = 0; b < MAX_BLOBS; b++) {
    if (!blobs[b].active) continue;

    blobs[b].pos += blobs[b].velocity * dt * speedF;
    if (blobs[b].pos >= LED_COUNT_A) { blobs[b].active = false; continue; }

    uint32_t age = now - blobs[b].spawnMs;
    float fade = (ARRIVE_FADE_MS == 0) ? 1.0f : (float)age / (ARRIVE_FADE_MS / (speedF + 0.01f));
    uint8_t arriveK = constrain(fade * 255, 0, 255);

    // Sub-pixel rendering range
    int start = floor(blobs[b].pos);
    int end = ceil(blobs[b].pos + blobs[b].len);

    for (int i = start; i < end; i++) {
      if (i < 0 || i >= LED_COUNT_A) continue;

      float rel = (float)i - blobs[b].pos;
      float feather = 6.0f; // Slightly wider feather for slow speeds
      float kF = 1.0f;

      // NEW: Non-linear Cosine Interpolation for "Buttery" Transitions
      // Transition = 0.5 * (1 - cos(PI * progress))
      if (rel < feather) {
        kF = 0.5f * (1.0f - cosf(PI * (rel / feather)));
      } else if (rel > (blobs[b].len - feather)) {
        float progress = (blobs[b].len - rel) / feather;
        kF = 0.5f * (1.0f - cosf(PI * progress));
      }
      
      uint8_t intense = scaleU8(constrain(kF * 255, 0, 255), arriveK);

      uint32_t curr = stripA.getPixelColor(i);
      uint8_t r8 = (curr >> 16) & 0xFF;
      uint8_t g8 = (curr >> 8) & 0xFF;
      uint8_t b8 = curr & 0xFF;
      uint8_t w8 = (curr >> 24) & 0xFF;

      stripA.setPixelColor(i, 
        addClamp(r8, scaleU8(blobs[b].r, intense)),
        addClamp(g8, scaleU8(blobs[b].g, intense)),
        addClamp(b8, scaleU8(blobs[b].b, intense)),
        addClamp(w8, scaleU8(blobs[b].w, intense))
      );
    }
  }

  // Mirror to Strip B
  for (int i = 0; i < LED_COUNT_B; i++) {
    uint32_t c = stripA.getPixelColor(i);
    uint8_t r = (c >> 16) & 0xFF;
    uint8_t g = (c >> 8) & 0xFF;
    uint8_t b = c & 0xFF;
    uint8_t w = (c >> 24) & 0xFF;
    stripB.setPixelColor(i, addClamp(r, w), addClamp(g, w), addClamp(b, w));
  }

  stripA.show();
  stripB.show();
}

uint32_t lastM = 0;
float spawnW = 0, spawnT = 0;

void setup() {
  stripA.begin();
  stripB.begin();
  stripA.setBrightness(200);
  stripB.setBrightness(255);
  
  uint32_t now = millis();
  lastM = now;

  // Instant Spawn to avoid startup delay
  spawn(true, now);  
  spawn(false, now); 
  spawnW = WHITE_SPAWN_MS; 
  spawnT = TERRA_SPAWN_MS;
}

void loop() {
  uint32_t now = millis();
  float dt = (float)(now - lastM);
  lastM = now;

  float speedF = speedPct / 500.0f; 
  if (speedPct > 0) {
    spawnW += dt * speedF;
    spawnT += dt * speedF;
    if (spawnW >= WHITE_SPAWN_MS) { spawnW = 0; spawn(true, now); }
    if (spawnT >= TERRA_SPAWN_MS) { spawnT = 0; spawn(false, now); }
  }

  render(now, dt);
}