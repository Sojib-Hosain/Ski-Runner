#include "raylib.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define SCREEN_W 900
#define SCREEN_H 520

// ---- Physics ----
#define GRAVITY     1850.0f
#define JUMP_VEL    -560.0f
#define HOLD_BOOST  1050.0f
#define DT_MAX      (1.0f/30.0f)   // clamp dt to avoid spikes

// ---- Gameplay ----
#define COIN_SCORE  100

#define MAX_OBS   64
#define MAX_COINS 96
#define MAX_SNOW  160

// ✅ world ceiling so character can't go outside map
#define CEILING_Y  0.0f

typedef enum { MENU=0, PLAYING, GAMEOVER } GameState;
typedef enum { OBS_TREE=0, OBS_ROCK=1 } ObstacleType;

typedef struct {
    float xw, yw;
    float w, h;
    ObstacleType type;
    bool active;
} Obstacle;

typedef struct {
    float xw, yw;
    float r;
    bool active;
} Coin;

typedef struct {
    float xw, yw;
    float vy;
    float drift;
    float size;
    float phase;
} Snow;

typedef struct {
    float xw;      // world x (kept fixed on screen)
    float y;       // world y
    float vy;
    float w, h;
    bool onGround;
} Player;

// ---------- Helpers ----------
static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

static float Randf(float minv, float maxv) {
    return minv + (maxv - minv) * ((float)rand() / (float)RAND_MAX);
}

static int Randi(int a, int b) { // inclusive
    return a + (rand() % (b - a + 1));
}

// ---------- Terrain ----------
static float GroundY(float worldX) {
    float base = (float)SCREEN_H - 110.0f;
    float y = base
        + 38.0f * sinf(worldX * 0.010f)
        + 18.0f * sinf(worldX * 0.023f + 1.7f)
        + 10.0f * sinf(worldX * 0.041f + 0.4f);
    return y;
}

// ---------- Save / Load High Score ----------
static int LoadHighScore(void) {
    FILE *f = fopen("highscore.txt", "r");
    if (!f) return 0;
    int hs = 0;
    fscanf(f, "%d", &hs);
    fclose(f);
    return hs;
}

static void SaveHighScore(int hs) {
    FILE *f = fopen("highscore.txt", "w");
    if (!f) return;
    fprintf(f, "%d\n", hs);
    fclose(f);
}

// ============================================================================
// BACKGROUND (Sky + sun glow + light rays + TRIANGLE mountains + fog)
// ============================================================================

static void DrawSkyGradient(void)
{
    Color top    = (Color){ 190, 220, 235, 255 };
    Color bottom = (Color){ 235, 248, 255, 255 };

    for (int y = 0; y < SCREEN_H; y++) {
        float t = (float)y / (float)SCREEN_H;
        Color c = (Color){
            (unsigned char)(top.r + (bottom.r - top.r) * t),
            (unsigned char)(top.g + (bottom.g - top.g) * t),
            (unsigned char)(top.b + (bottom.b - top.b) * t),
            255
        };
        DrawRectangle(0, y, SCREEN_W, 1, c);
    }

    // soft sun glow
    DrawCircleGradient((int)(SCREEN_W * 0.58f), (int)(SCREEN_H * 0.12f), 240,
                       (Color){255,255,255,125}, (Color){255,255,255,0});
}

static void DrawLightRays(void)
{
    float t = (float)GetTime();
    for (int i = 0; i < 4; i++)
    {
        float x = SCREEN_W * 0.22f + i * 190.0f + sinf(t * 0.6f + i) * 18.0f;

        DrawRectanglePro(
            (Rectangle){ x, -200, 46, SCREEN_H * 2.0f },
            (Vector2){ 23, 0 },
            15.0f,
            (Color){255,255,255,22}
        );
    }
}

/*
  Triangle mountain layer (Alto vibe)
*/
static void DrawTriangleMountains(float camX, float parallax, float yBase, float step, float peakMin, float peakVar, Color col)
{
    float scroll = fmodf(camX * parallax, step);
    for (int i = -4; i < 16; i++) {
        float x0 = i * step - scroll;
        float cx = x0 + step * 0.5f;

        float h = peakMin + peakVar * (0.5f + 0.5f * sinf(i * 0.85f + camX * 0.0012f));
        float peakY = yBase - h;

        Vector2 pTop = (Vector2){ cx, peakY };
        Vector2 pL   = (Vector2){ x0, yBase };
        Vector2 pR   = (Vector2){ x0 + step, yBase };

        DrawTriangle(pTop, pL, pR, col);
    }
}

static void DrawFogBand(void)
{
    DrawRectangleGradientV(0, (int)(SCREEN_H * 0.42f), SCREEN_W, (int)(SCREEN_H * 0.60f),
                           (Color){255,255,255,65}, (Color){255,255,255,0});
}

static void DrawAltoBackground(float worldX, float camY)
{
    (void)camY;
    DrawSkyGradient();
    DrawLightRays();

    DrawTriangleMountains(worldX, 0.08f, SCREEN_H * 0.62f, 280.0f, 90.0f, 90.0f, (Color){ 120, 155, 165, 55 });
    DrawTriangleMountains(worldX, 0.14f, SCREEN_H * 0.72f, 240.0f, 80.0f, 85.0f, (Color){  95, 140, 152, 80 });
    DrawTriangleMountains(worldX, 0.22f, SCREEN_H * 0.82f, 220.0f, 70.0f, 80.0f, (Color){  75, 125, 140, 105 });

    DrawFogBand();
}

// ============================================================================
// Hills (✅ only changed colors)
// ============================================================================

static void DrawHills(float worldX, float camY) {
    const float step = 10.0f;

    // ✅ MORE visible sky-blue surface (non-transparent)
    Color snowFill = (Color){ 70, 155, 215, 255 };   // visible surface
    Color snowLine = (Color){ 45, 95, 135, 255 };   // darker edge line
    for (float sx = 0; sx < SCREEN_W; sx += step) {
        float wx1 = worldX + sx;
        float wx2 = worldX + (sx + step);

        float y1 = GroundY(wx1) - camY;
        float y2 = GroundY(wx2) - camY;

        DrawTriangle((Vector2){sx, y1}, (Vector2){sx + step, y2}, (Vector2){sx, (float)SCREEN_H}, snowFill);
        DrawTriangle((Vector2){sx + step, y2}, (Vector2){sx + step, (float)SCREEN_H}, (Vector2){sx, (float)SCREEN_H}, snowFill);
        DrawLineEx((Vector2){sx, y1}, (Vector2){sx + step, y2}, 4.0f, snowLine);          // main border
        DrawLineEx((Vector2){sx, y1}, (Vector2){sx + step, y2}, 1.5f, (Color){255,255,255,120});  // shine line
    }
}

// ============================================================================
// Snow (world-space)
// ============================================================================

static void InitSnow(Snow snow[MAX_SNOW], float worldX) {
    for (int i = 0; i < MAX_SNOW; i++) {
        snow[i].xw = worldX + Randf(0, (float)SCREEN_W);
        snow[i].yw = Randf(-50, (float)SCREEN_H);
        snow[i].vy = Randf(30.0f, 120.0f);
        snow[i].drift = Randf(-18.0f, 18.0f);
        snow[i].size = Randf(1.0f, 2.6f);
        snow[i].phase = Randf(0.0f, 10.0f);
    }
}

static void UpdateSnow(Snow snow[MAX_SNOW], float dt, float worldX) {
    float t = (float)GetTime();
    float wind = sinf(t * 0.6f) * 18.0f;

    for (int i = 0; i < MAX_SNOW; i++) {
        snow[i].yw += snow[i].vy * dt;
        snow[i].xw += (snow[i].drift + wind + sinf(t + snow[i].phase) * 6.0f) * dt;

        if (snow[i].yw > (float)SCREEN_H + 10) {
            snow[i].yw = Randf(-60, -10);
            snow[i].xw = worldX + Randf(0, (float)SCREEN_W);
        }
        if (snow[i].xw < worldX - 20) snow[i].xw = worldX + SCREEN_W + 20;
        if (snow[i].xw > worldX + SCREEN_W + 20) snow[i].xw = worldX - 20;
    }
}

static void DrawSnow(const Snow snow[MAX_SNOW], float worldX, float camY) {
    for (int i = 0; i < MAX_SNOW; i++) {
        float sx = snow[i].xw - worldX;
        float sy = snow[i].yw - camY;

        if (sx >= -10 && sx <= SCREEN_W + 10 && sy >= -20 && sy <= SCREEN_H + 20) {
            unsigned char a = (unsigned char)(90 + (snow[i].size * 35.0f));
            DrawCircleV((Vector2){sx, sy}, snow[i].size, (Color){255, 255, 255, a});
        }
    }
}

// ---------- Obstacles + Coins ----------
static void ClearObstacles(Obstacle obs[MAX_OBS]) { for (int i=0;i<MAX_OBS;i++) obs[i].active=false; }
static void ClearCoins(Coin coins[MAX_COINS]) { for (int i=0;i<MAX_COINS;i++) coins[i].active=false; }

static void SpawnObstacle(Obstacle obs[MAX_OBS], float spawnXw) {
    for (int i = 0; i < MAX_OBS; i++) {
        if (!obs[i].active) {
            obs[i].active = true;
            obs[i].xw = spawnXw;

            float gy = GroundY(spawnXw);
            obs[i].type = (Randi(0, 100) < 60) ? OBS_TREE : OBS_ROCK;

            if (obs[i].type == OBS_TREE) {
                obs[i].w = 32; obs[i].h = 58;
                obs[i].yw = gy - obs[i].h;
            } else {
                obs[i].w = 40; obs[i].h = 26;
                obs[i].yw = gy - obs[i].h;
            }
            return;
        }
    }
}

static void SpawnCoin(Coin coins[MAX_COINS], float spawnXw) {
    for (int i = 0; i < MAX_COINS; i++) {
        if (!coins[i].active) {
            coins[i].active = true;
            coins[i].xw = spawnXw;

            float gy = GroundY(spawnXw);
            coins[i].r = 9.0f;
            coins[i].yw = gy - 70.0f - Randf(0, 30);
            return;
        }
    }
}

static void DespawnFarStuff(Obstacle obs[MAX_OBS], Coin coins[MAX_COINS], float worldX) {
    float leftKill = worldX - 200.0f;
    for (int i = 0; i < MAX_OBS; i++) if (obs[i].active && obs[i].xw < leftKill) obs[i].active = false;
    for (int i = 0; i < MAX_COINS; i++) if (coins[i].active && coins[i].xw < leftKill) coins[i].active = false;
}

// ✅ Player hitbox (already in your code)
static Rectangle PlayerRectWorld(Player *p)
{
    float insetX = p->w * 0.22f;
    float insetY = p->h * 0.12f;

    return (Rectangle){
        (p->xw - p->w*0.5f) + insetX,
        p->y + insetY,
        p->w - insetX*2.0f,
        p->h - insetY*2.0f
    };
}

// ✅ FIX: ObstacleRectWorld was missing -> build error
static Rectangle ObstacleRectWorld(Obstacle *o)
{
    float insetX = o->w * 0.18f;
    float insetY = o->h * 0.10f;

    return (Rectangle){
        (o->xw - o->w*0.5f) + insetX,
        o->yw + insetY,
        o->w - insetX*2.0f,
        o->h - insetY*2.0f
    };
}

// ---------- Reset ----------
static void ResetGame(Player *p,
                      float *worldX, float *speed, float *distance,
                      Obstacle obs[MAX_OBS], Coin coins[MAX_COINS],
                      int *coinsCollected,
                      float *nextObsAt, float *nextCoinAt,
                      float *camY) {
    *worldX = 0.0f;
    *speed = 260.0f;
    *distance = 0.0f;
    *coinsCollected = 0;

    ClearObstacles(obs);
    ClearCoins(coins);

    p->w = 28;
    p->h = 32;
    p->xw = (*worldX) + 180.0f;
    p->vy = 0.0f;
    p->onGround = true;

    float gy = GroundY(p->xw);
    p->y = gy - p->h;

    *camY = p->y - 240.0f;
    if (*camY < 0) *camY = 0;

    *nextObsAt = 280.0f;
    *nextCoinAt = 200.0f;
}

// ---------- Draw obstacle ----------
static void DrawObstacle(Obstacle *o, float worldX, float camY) {
    float sx = o->xw - worldX;
    float sy = o->yw - camY;

    if (o->type == OBS_TREE) {
        DrawRectangle((int)(sx - 6), (int)(sy + o->h - 22), 12, 22, (Color){120, 90, 60, 255});
        DrawTriangle((Vector2){sx, sy}, (Vector2){sx - 22, sy + 38}, (Vector2){sx + 22, sy + 38}, (Color){35, 110, 70, 255});
        DrawTriangle((Vector2){sx, sy + 16}, (Vector2){sx - 18, sy + 52}, (Vector2){sx + 18, sy + 52}, (Color){40, 125, 78, 255});
    } else {
        DrawRectangleRounded((Rectangle){sx - o->w*0.5f, sy, o->w, o->h}, 0.35f, 8, (Color){90, 100, 110, 255});
        DrawLine((int)(sx - 10), (int)(sy + 10), (int)(sx + 14), (int)(sy + 16), (Color){130, 140, 150, 130});
    }
}

// ---------- Draw coin ----------
static void DrawCoin(Coin *c, float worldX, float camY) {
    float sx = c->xw - worldX;
    float sy = c->yw - camY;

    Color gold = (Color){240, 190, 60, 255};
    Color shine = (Color){255, 245, 200, 200};

    DrawCircleV((Vector2){sx, sy}, c->r, gold);
    DrawCircleV((Vector2){sx - 2, sy - 2}, c->r * 0.45f, shine);
    DrawCircleLines((int)sx, (int)sy, c->r, (Color){120, 90, 30, 140});
}

int main(void) {
    InitWindow(SCREEN_W, SCREEN_H, "Ski Runner - C + raylib (Coin Score)");
    SetTargetFPS(60);

    InitAudioDevice();
    Music bgMusic = LoadMusicStream("resources/pripac-chill-gaming-beats-323454.mp3");
    SetMusicVolume(bgMusic, 0.5f);

    srand(2026);

    GameState state = MENU;

    Player player = (Player){0};
    Obstacle obs[MAX_OBS];
    Coin coins[MAX_COINS];
    Snow snow[MAX_SNOW];

    float worldX = 0.0f;
    float speed = 260.0f;
    float distance = 0.0f;

    float camY = 0.0f;
    float nextObsAt = 280.0f;
    float nextCoinAt = 200.0f;

    int coinsCollected = 0;
    int highScore = LoadHighScore();

    ResetGame(&player, &worldX, &speed, &distance, obs, coins, &coinsCollected, &nextObsAt, &nextCoinAt, &camY);
    InitSnow(snow, worldX);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > DT_MAX) dt = DT_MAX;

        UpdateMusicStream(bgMusic);

        // MENU / GAMEOVER input
        if (state == MENU) {
            if (IsKeyPressed(KEY_ENTER)) {
                ResetGame(&player, &worldX, &speed, &distance, obs, coins, &coinsCollected, &nextObsAt, &nextCoinAt, &camY);
                InitSnow(snow, worldX);
                if (!IsMusicStreamPlaying(bgMusic)) PlayMusicStream(bgMusic);
                state = PLAYING;
            }
        } else if (state == GAMEOVER) {
            if (IsKeyPressed(KEY_R)) {
                ResetGame(&player, &worldX, &speed, &distance, obs, coins, &coinsCollected, &nextObsAt, &nextCoinAt, &camY);
                InitSnow(snow, worldX);
                if (!IsMusicStreamPlaying(bgMusic)) PlayMusicStream(bgMusic);
                state = PLAYING;
            }
            if (IsKeyPressed(KEY_ESCAPE)) state = MENU;
        }

        // PLAYING update
        if (state == PLAYING) {
            speed += 6.0f * dt;

            worldX += speed * dt;
            distance += speed * dt;

            player.xw = worldX + 180.0f;

            // ✅ jump every press (even mid-air), BUT won't go out of map
            if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP)) {
                player.vy = JUMP_VEL;
                player.onGround = false;
            }

            // Variable jump hold
            if (!player.onGround && (IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_UP)) && player.vy < 0.0f) {
                player.vy -= HOLD_BOOST * dt;
            }

            // Gravity + move
            player.vy += GRAVITY * dt;
            player.y += player.vy * dt;

            // ✅ Ceiling clamp (top boundary)
            if (player.y < CEILING_Y) {
                player.y = CEILING_Y;
                if (player.vy < 0.0f) player.vy = 0.0f;
            }

            // Ground collision
            float gy = GroundY(player.xw);
            if (player.y + player.h >= gy) {
                player.y = gy - player.h;
                player.vy = 0.0f;
                player.onGround = true;
            } else {
                player.onGround = false;
            }

            // Spawn obstacles / coins
            if (distance >= nextObsAt) {
                float spawnXw = worldX + (float)SCREEN_W + Randf(120.0f, 420.0f);
                SpawnObstacle(obs, spawnXw);
                nextObsAt += Randf(220.0f, 420.0f);
            }
            if (distance >= nextCoinAt) {
                float spawnXw = worldX + (float)SCREEN_W + Randf(60.0f, 280.0f);
                SpawnCoin(coins, spawnXw);
                nextCoinAt += Randf(120.0f, 260.0f);
            }

            DespawnFarStuff(obs, coins, worldX);
            UpdateSnow(snow, dt, worldX);

            // camera follow
            float targetCamY = player.y - 240.0f;
            if (targetCamY < 0) targetCamY = 0;
            camY = Lerp(camY, targetCamY, 0.08f);

            // obstacle collision -> gameover
            Rectangle pr = PlayerRectWorld(&player);
            for (int i = 0; i < MAX_OBS; i++) {
                if (!obs[i].active) continue;
                if (CheckCollisionRecs(pr, ObstacleRectWorld(&obs[i]))) {
                    state = GAMEOVER;

                    int score = coinsCollected * COIN_SCORE;
                    if (score > highScore) {
                        highScore = score;
                        SaveHighScore(highScore);
                    }
                    break;
                }
            }

            // coin collect
            for (int i = 0; i < MAX_COINS; i++) {
                if (!coins[i].active) continue;
                Vector2 cc = (Vector2){ coins[i].xw, coins[i].yw };
                if (CheckCollisionCircleRec(cc, coins[i].r, pr)) {
                    coins[i].active = false;
                    coinsCollected++;
                }
            }
        }

        // DRAW (order)
        BeginDrawing();

        DrawAltoBackground(worldX, camY);
        DrawSnow(snow, worldX, camY);
        DrawHills(worldX, camY);

        // coins
        for (int i = 0; i < MAX_COINS; i++) {
            if (!coins[i].active) continue;
            float sx = coins[i].xw - worldX;
            if (sx > -50 && sx < SCREEN_W + 50) DrawCoin(&coins[i], worldX, camY);
        }

        // obstacles
        for (int i = 0; i < MAX_OBS; i++) {
            if (!obs[i].active) continue;
            float sx = obs[i].xw - worldX;
            if (sx > -80 && sx < SCREEN_W + 80) DrawObstacle(&obs[i], worldX, camY);
        }

        // player
        float px = player.xw - worldX;
        float py = player.y - camY;
        Rectangle body = (Rectangle){ px - player.w*0.5f, py, player.w, player.h };
        DrawRectangleRounded(body, 0.25f, 8, (Color){220, 60, 60, 255});
        DrawCircle((int)(px + player.w*0.15f), (int)(py + 8), 5, (Color){35, 40, 45, 255});
        DrawLine((int)(px - 8), (int)(py + player.h - 6), (int)(px + 14), (int)(py + player.h - 2), (Color){40, 45, 50, 200});

        // UI
        int distDisplay = (int)(distance / 2.0f);
        int totalScore = coinsCollected * COIN_SCORE;

        DrawText(TextFormat("%dm", distDisplay), SCREEN_W - 170, 18, 28, (Color){30, 40, 50, 200});
        DrawText(TextFormat("Coins: %d", coinsCollected), SCREEN_W - 170, 52, 18, (Color){30, 40, 50, 180});
        DrawText(TextFormat("Score: %d", totalScore), SCREEN_W - 170, 76, 18, (Color){30, 40, 50, 180});
        DrawText(TextFormat("High: %d", highScore), SCREEN_W - 170, 100, 18, (Color){30, 40, 50, 180});

        if (state == MENU) {
            DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){0, 0, 0, 90});
            DrawText("SKI RUNNER", 48, 54, 56, RAYWHITE);
            DrawText("ENTER: Start", 52, 130, 22, RAYWHITE);
            DrawText("SPACE / UP: Jump", 52, 160, 22, RAYWHITE);
            DrawText("Collect coins for score!", 52, 205, 20, (Color){230,240,250,220});
        }

        if (state == GAMEOVER) {
            DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){0, 0, 0, 130});
            DrawText("GAME OVER", 48, 54, 56, RAYWHITE);
            DrawText(TextFormat("Score: %d", totalScore), 52, 125, 26, RAYWHITE);
            DrawText("R: Restart", 52, 170, 22, RAYWHITE);
            DrawText("ESC: Menu", 52, 200, 22, RAYWHITE);
        }

        EndDrawing();
    }

    UnloadMusicStream(bgMusic);
    CloseAudioDevice();
    CloseWindow();
    return 0;
}