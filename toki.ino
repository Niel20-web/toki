#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <LittleFS.h>
#include <AudioFileSourceLittleFS.h>
#include <AudioGeneratorWAV.h>
#include <AudioOutputI2S.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SH1106G display(128, 64, &Wire, -1);

AudioGeneratorWAV *wav = nullptr;
AudioFileSourceLittleFS *audioFile = nullptr;
AudioOutputI2S *audioOutput = nullptr;

const int EYE_RADIUS = 12;
const int PUPIL_RADIUS = 5;

const int LEFT_EYE_X  = 35;
const int LEFT_EYE_Y  = 32;

const int RIGHT_EYE_X = 93;
const int RIGHT_EYE_Y = 32;
int pupilOffsetX = 0;
int pupilOffsetY = 0;
int eyeoffsetx=0;
int eyeoffsety=0;

//==============================
// Pupil Movement
//==============================
int targetEyeOffsetX = 0;
int targetEyeOffsetY = 0;
int targetPupilOffsetX = 0;
int targetPupilOffsetY = 0;


int eyeOpen = EYE_RADIUS;

//==============================
// Setup
//==============================


// ===== Emotion System =====
enum Emotion{NEUTRAL,HAPPY,ANGRY,CURIOUS,SURPRISED,SLEEPING};
Emotion currentEmotion=NEUTRAL;
void drawBrows(Emotion e); // explicit prototype, prevents Arduino's auto-generated one from landing above the enum
void drawBrows(Emotion e){
 switch(e){
 case HAPPY: drawHappyBrows(); break;
 case ANGRY: drawAngryBrows(); break;
 case CURIOUS: drawCuriousBrows(); break;
 case SURPRISED: drawSurprisedBrows(); break;
 case SLEEPING: drawSleepingBrows(); break;
 default: drawLeftBrow(); drawRightBrow(); break;
 }}
void initFace()
{
    Wire.begin();
    Wire.setClock(400000);

    if (!display.begin(0x3C, true))
    {
        while(true);
    }

    display.clearDisplay();
    display.display();
}

void audioSetup()
{
    if (!LittleFS.begin(true))
    {
        Serial.println("LittleFS FAILED");
        return;
    }

    Serial.println("LittleFS OK");

    audioOutput = new AudioOutputI2S();

    // BCLK, LRCLK, DIN
    audioOutput->SetPinout(8, 7, 9);

    audioOutput->SetGain(0.5);

    Serial.println("Audio ready");
}

void setup() {
  initFace();
  audioSetup();

}


//==============================
// Loop
//==============================

void loop()
{
    // =========================
    // STARTUP
    // =========================

    playSound("/startup_alt.wav");
    startup();

    delay(500);


    // =========================
    // NORMAL MOVEMENT
    // =========================

    blink();
    delay(500);

    lookLeft();
    moveEyes();
    delay(500);

    lookRight();
    moveEyes();
    delay(500);

    lookCenter();
    moveEyes();
    delay(1000);


    // =========================
    // HAPPY
    // =========================

    playSound("/happy.wav");
    happyBounce();

    while (wav && wav->isRunning())
    {
        audioLoop();
    }

    delay(500);


    // =========================
    // CURIOUS
    // =========================

    playSound("/curious.wav");
    curiousAnimation();

    while (wav && wav->isRunning())
    {
        audioLoop();
    }

    delay(500);


    // =========================
    // ANGRY
    // =========================

    playSound("/angry2.wav");
    angryShake();

    while (wav && wav->isRunning())
    {
        audioLoop();
    }

    delay(500);


    // =========================
    // SURPRISED
    // =========================

    playSound("/surprise_1.wav");
    surpriseAnimation();

    while (wav && wav->isRunning())
    {
        audioLoop();
    }

    delay(500);

// =========================
// SLEEP
// =========================



sleeping();
sleepBreathing();

while (wav && wav->isRunning())
{
    audioLoop();
}

delay(500);


    // =========================
    // WAKE UP
    // =========================

    playSound("/wake_up_2.wav");
    wakeup();

    while (wav && wav->isRunning())
    {
        audioLoop();
    }

    delay(1000);


    // =========================
    // IDLE
    // =========================

    idleAnimation();

    delay(3000);
}
//==============================
// Draw One Frame
//==============================
void drawFrame()
{
    display.clearDisplay();

    drawFace();

    display.display();
}

//==============================
// Draw Eye
//==============================
void drawEye(int x, int y)
{
    // Draw the white eye
    display.fillCircle(
        x + eyeoffsetx,
        y + eyeoffsety,
        EYE_RADIUS,
        SH110X_WHITE
    );

    // Hide the top part of the eye
    display.fillRect(
        x - EYE_RADIUS + eyeoffsetx,
        y - EYE_RADIUS + eyeoffsety,
        EYE_RADIUS * 2,
        EYE_RADIUS - eyeOpen,
        SH110X_BLACK
    );

    // Hide the bottom part
    display.fillRect(
        x - EYE_RADIUS + eyeoffsetx,
        y + eyeOpen + eyeoffsety,
        EYE_RADIUS * 2,
        EYE_RADIUS - eyeOpen,
        SH110X_BLACK
    );

    // Only draw pupil if eye is open enough
    if (eyeOpen > 3)
    {
        display.fillCircle(
            x + eyeoffsetx + pupilOffsetX,
            y + eyeoffsety + pupilOffsetY,
            PUPIL_RADIUS,
            SH110X_BLACK
        );
    }
}
//==============================
// Left Eyebrow
//==============================
void drawLeftBrow()
{
    display.drawLine(18,22,24,18,SH110X_WHITE);
    display.drawLine(24,18,30,15,SH110X_WHITE);
    display.drawLine(30,15,35,17,SH110X_WHITE);

    display.drawLine(18,23,24,19,SH110X_WHITE);
    display.drawLine(24,19,30,16,SH110X_WHITE);
    display.drawLine(30,16,35,18,SH110X_WHITE);

    display.drawLine(18,24,24,20,SH110X_WHITE);
    display.drawLine(24,20,30,17,SH110X_WHITE);
    display.drawLine(30,17,35,19,SH110X_WHITE);
}

//==============================
// Right Eyebrow
//==============================
void drawRightBrow()
{
    display.drawLine(93,17,98,15,SH110X_WHITE);
    display.drawLine(98,15,104,18,SH110X_WHITE);
    display.drawLine(104,18,110,22,SH110X_WHITE);

    display.drawLine(93,18,98,16,SH110X_WHITE);
    display.drawLine(98,16,104,19,SH110X_WHITE);
    display.drawLine(104,19,110,23,SH110X_WHITE);

    display.drawLine(93,19,98,17,SH110X_WHITE);
    display.drawLine(98,17,104,20,SH110X_WHITE);
    display.drawLine(104,20,110,24,SH110X_WHITE);
}

//==============================
// Happy Brows (raised, gentle arch)
//==============================
void drawHappyBrows()
{
    display.drawLine(18,19,24,15,SH110X_WHITE);
    display.drawLine(24,15,30,12,SH110X_WHITE);
    display.drawLine(30,12,35,14,SH110X_WHITE);

    display.drawLine(18,20,24,16,SH110X_WHITE);
    display.drawLine(24,16,30,13,SH110X_WHITE);
    display.drawLine(30,13,35,15,SH110X_WHITE);

    display.drawLine(18,21,24,17,SH110X_WHITE);
    display.drawLine(24,17,30,14,SH110X_WHITE);
    display.drawLine(30,14,35,16,SH110X_WHITE);

    display.drawLine(93,14,98,12,SH110X_WHITE);
    display.drawLine(98,12,104,15,SH110X_WHITE);
    display.drawLine(104,15,110,19,SH110X_WHITE);

    display.drawLine(93,15,98,13,SH110X_WHITE);
    display.drawLine(98,13,104,16,SH110X_WHITE);
    display.drawLine(104,16,110,20,SH110X_WHITE);

    display.drawLine(93,16,98,14,SH110X_WHITE);
    display.drawLine(98,14,104,17,SH110X_WHITE);
    display.drawLine(104,17,110,21,SH110X_WHITE);
}

//==============================
// Angry Brows (slant down toward center)
//==============================
void drawAngryBrows()
{
    // Left eyebrow (slants downward toward the center)
    for(int i = 0; i < 3; i++)
    {
        display.drawLine(
            LEFT_EYE_X - 12,
            LEFT_EYE_Y - 14 + i,
            LEFT_EYE_X + 2,
            LEFT_EYE_Y - 8 + i,
            SH110X_WHITE
        );
    }

    // Right eyebrow (mirror image)
    for(int i = 0; i < 3; i++)
    {
        display.drawLine(
            RIGHT_EYE_X - 2,
            RIGHT_EYE_Y - 8 + i,
            RIGHT_EYE_X + 12,
            RIGHT_EYE_Y - 14 + i,
            SH110X_WHITE
        );
    }
}

//==============================
// Curious Brows (one raised, quizzical)
//==============================
void drawCuriousBrows()
{
    drawLeftBrow();

    display.drawLine(93,12,98,9,SH110X_WHITE);
    display.drawLine(98,9,104,11,SH110X_WHITE);
    display.drawLine(104,11,110,16,SH110X_WHITE);

    display.drawLine(93,13,98,10,SH110X_WHITE);
    display.drawLine(98,10,104,12,SH110X_WHITE);
    display.drawLine(104,12,110,17,SH110X_WHITE);

    display.drawLine(93,14,98,11,SH110X_WHITE);
    display.drawLine(98,11,104,13,SH110X_WHITE);
    display.drawLine(104,13,110,18,SH110X_WHITE);
}

//==============================
// Surprised Brows (both raised high)
//==============================
void drawSurprisedBrows()
{
    display.drawLine(18,16,24,11,SH110X_WHITE);
    display.drawLine(24,11,30,8,SH110X_WHITE);
    display.drawLine(30,8,35,10,SH110X_WHITE);

    display.drawLine(18,17,24,12,SH110X_WHITE);
    display.drawLine(24,12,30,9,SH110X_WHITE);
    display.drawLine(30,9,35,11,SH110X_WHITE);

    display.drawLine(18,18,24,13,SH110X_WHITE);
    display.drawLine(24,13,30,10,SH110X_WHITE);
    display.drawLine(30,10,35,12,SH110X_WHITE);

    display.drawLine(93,10,98,8,SH110X_WHITE);
    display.drawLine(98,8,104,11,SH110X_WHITE);
    display.drawLine(104,11,110,16,SH110X_WHITE);

    display.drawLine(93,11,98,9,SH110X_WHITE);
    display.drawLine(98,9,104,12,SH110X_WHITE);
    display.drawLine(104,12,110,17,SH110X_WHITE);

    display.drawLine(93,12,98,10,SH110X_WHITE);
    display.drawLine(98,10,104,13,SH110X_WHITE);
    display.drawLine(104,13,110,18,SH110X_WHITE);
}

//==============================
// Sleeping Brows (relaxed, curved)
//==============================
void drawSleepingBrows()
{
    // Smooth curved arc (like "(" / ")" laid on its side) — several short
    // segments approximate a gentle, rounded, relaxed brow.
    int leftX[]  = {18, 21, 24, 27, 30, 33, 35};
    int leftY[]  = {22, 19, 17, 16, 17, 19, 22};
    for (int i = 0; i < 6; i++)
    {
        display.drawLine(leftX[i], leftY[i], leftX[i + 1], leftY[i + 1], SH110X_WHITE);
    }

    int rightX[] = {93, 96, 99, 102, 105, 108, 110};
    int rightY[] = {22, 19, 17, 16, 17, 19, 22};
    for (int i = 0; i < 6; i++)
    {
        display.drawLine(rightX[i], rightY[i], rightX[i + 1], rightY[i + 1], SH110X_WHITE);
    }
}
//==============================
// Face
//==============================
void drawFace()
{
    drawEye(LEFT_EYE_X, LEFT_EYE_Y);
    drawEye(RIGHT_EYE_X, RIGHT_EYE_Y);

    drawBrows(currentEmotion);
    
}
void lookCenter()
{
    targetEyeOffsetX = 0;
    targetEyeOffsetY = 0;

    targetPupilOffsetX = 0;
    targetPupilOffsetY = 0;
}

void lookLeft()
{
    targetEyeOffsetX = -3;
    targetEyeOffsetY = 0;

    targetPupilOffsetX = -5;
    targetPupilOffsetY = 0;
}

void lookRight()
{
    targetEyeOffsetX = 3;
    targetEyeOffsetY = 0;

    targetPupilOffsetX = 5;
    targetPupilOffsetY = 0;
}

void lookUp()
{
    targetEyeOffsetX = 0;
    targetEyeOffsetY = -3;

    targetPupilOffsetX = 0;
    targetPupilOffsetY = -5;
}

void lookDown()
{
    targetEyeOffsetX = 0;
    targetEyeOffsetY = 3;

    targetPupilOffsetX = 0;
    targetPupilOffsetY = 5;
}

void moveEyes()
{
    while (eyeoffsetx != targetEyeOffsetX ||
           eyeoffsety != targetEyeOffsetY ||
           pupilOffsetX != targetPupilOffsetX ||
           pupilOffsetY != targetPupilOffsetY)
    {
        if (eyeoffsetx < targetEyeOffsetX) eyeoffsetx++;
        else if (eyeoffsetx > targetEyeOffsetX) eyeoffsetx--;

        if (eyeoffsety < targetEyeOffsetY) eyeoffsety++;
        else if (eyeoffsety > targetEyeOffsetY) eyeoffsety--;

        if (pupilOffsetX < targetPupilOffsetX) pupilOffsetX++;
        else if (pupilOffsetX > targetPupilOffsetX) pupilOffsetX--;

        if (pupilOffsetY < targetPupilOffsetY) pupilOffsetY++;
        else if (pupilOffsetY > targetPupilOffsetY) pupilOffsetY--;

        drawFrame();
        delay(5);
    }
}

void blink()
{
    for(int i = EYE_RADIUS; i >= 0; i -= 3)
    {
        eyeOpen = i;
        drawFrame();
        delay(3);
    }

    for(int i = 0; i <= EYE_RADIUS; i += 3)
    {
        eyeOpen = i;
        drawFrame();
        delay(3);
    }

    
}
void sleepy()
{
    // Start sleep sound once
    playSound("/sleep_toki.wav");

    while (eyeOpen > 4)
    {
        eyeOpen--;

        if (pupilOffsetY < 5)
        {
            pupilOffsetY++;
        }

        drawFrame();

        // Keep audio alive
        for (int t = 0; t < 3; t++)
        {
            audioLoop();
            delay(10);
        }
    }

    drawFrame();

    // 800 ms pause
    for (int t = 0; t < 80; t++)
    {
        audioLoop();
        delay(10);
    }

    // First sleepy blink
    sleepyblink();

    // 1500 ms pause
    for (int t = 0; t < 150; t++)
    {
        audioLoop();
        delay(10);
    }

    // Second sleepy blink
    sleepyblink();
}
void wakeup()
{
    currentEmotion = NEUTRAL;

    while (eyeOpen < EYE_RADIUS)
    {
        eyeOpen++;

        if (pupilOffsetY > 0)
        {
            pupilOffsetY--;
        }

        drawFrame();

        // Keep audio processing alive
        for (int t = 0; t < 3; t++)
        {
            audioLoop();
            delay(10);
        }
    }
}
void sleepyblink()
{
    // Close eyes
    while (eyeOpen > 2)
    {
        eyeOpen--;

        drawFrame();

        for (int t = 0; t < 4; t++)
        {
            audioLoop();
            delay(10);
        }
    }

    // Slightly open eyes again
    while (eyeOpen < 4)
    {
        eyeOpen++;

        drawFrame();

        for (int t = 0; t < 4; t++)
        {
            audioLoop();
            delay(10);
        }
    }
}
void happy()
{
    currentEmotion = HAPPY;

    eyeOpen = 5;
    eyeoffsety = -1;

    drawFrame();

    // Erase left pupil
    display.fillCircle(
        LEFT_EYE_X + eyeoffsetx + pupilOffsetX,
        LEFT_EYE_Y + eyeoffsety + pupilOffsetY,
        PUPIL_RADIUS,
        SH110X_WHITE
    );

    // Erase right pupil
    display.fillCircle(
        RIGHT_EYE_X + eyeoffsetx + pupilOffsetX,
        RIGHT_EYE_Y + eyeoffsety + pupilOffsetY,
        PUPIL_RADIUS,
        SH110X_WHITE
    );

    // Draw happy ^
    int lx = LEFT_EYE_X + eyeoffsetx;
    int ly = LEFT_EYE_Y + eyeoffsety;
// Left stroke
    display.drawLine(lx - 4, ly + 2, lx, ly - 2, SH110X_BLACK);
    display.drawLine(lx - 4, ly + 2, lx, ly - 2, SH110X_BLACK);
    display.drawLine(lx - 4, ly + 3, lx, ly - 1, SH110X_BLACK);
    display.drawLine(lx - 4, ly + 3, lx, ly - 1, SH110X_BLACK);

// Right stroke
   display.drawLine(lx, ly - 2, lx + 4, ly + 2, SH110X_BLACK);
   display.drawLine(lx, ly - 2, lx + 4, ly + 2, SH110X_BLACK);
   display.drawLine(lx, ly - 1, lx + 4, ly + 3, SH110X_BLACK);
    display.drawLine(lx, ly - 1, lx + 4, ly + 3, SH110X_BLACK);

    int rx = RIGHT_EYE_X + eyeoffsetx;
    int ry = RIGHT_EYE_Y + eyeoffsety;

    // Left stroke
    display.drawLine(rx - 4, ry + 2, rx, ry - 2, SH110X_BLACK);
    display.drawLine(rx - 4, ry + 2, rx, ry - 2, SH110X_BLACK);
   display.drawLine(rx - 4, ry + 3, rx, ry - 1, SH110X_BLACK);
   display.drawLine(rx - 4, ry + 3, rx, ry - 1, SH110X_BLACK);

// Right stroke
   display.drawLine(rx, ry - 2, rx + 4, ry + 2, SH110X_BLACK);
   display.drawLine(rx, ry - 2, rx + 4, ry + 2, SH110X_BLACK);
   display.drawLine(rx, ry - 1, rx + 4, ry + 3, SH110X_BLACK);
   display.drawLine(rx, ry - 1, rx + 4, ry + 3, SH110X_BLACK);

    display.display();
}

void drawSleepingEyes()
{
    int lx = LEFT_EYE_X + eyeoffsetx;
    int ly = LEFT_EYE_Y + eyeoffsety;

    int rx = RIGHT_EYE_X + eyeoffsetx;
    int ry = RIGHT_EYE_Y + eyeoffsety;

    // Erase eyes
    display.fillCircle(lx, ly, EYE_RADIUS, SH110X_BLACK);
    display.fillCircle(rx, ry, EYE_RADIUS, SH110X_BLACK);

    // Left eye  \____/
    display.drawLine(lx - 8, ly,     lx - 3, ly + 2, SH110X_WHITE);
    display.drawLine(lx - 3, ly + 2, lx + 3, ly + 2, SH110X_WHITE);
    display.drawLine(lx + 3, ly + 2, lx + 8, ly,     SH110X_WHITE);

    // Right eye  \____/
    display.drawLine(rx - 8, ry,     rx - 3, ry + 2, SH110X_WHITE);
    display.drawLine(rx - 3, ry + 2, rx + 3, ry + 2, SH110X_WHITE);
    display.drawLine(rx + 3, ry + 2, rx + 8, ry,     SH110X_WHITE);
}

void drawSleepZ(int x, int y)
{
    // Top
    display.drawLine(x, y, x + 4, y, SH110X_WHITE);

    // Diagonal
    display.drawLine(x + 4, y, x, y + 5, SH110X_WHITE);

    // Bottom
    display.drawLine(x, y + 5, x + 4, y + 5, SH110X_WHITE);
}

void sleeping()
{
    currentEmotion = SLEEPING;

    sleepy();

    for (int i = 0; i < 40; i++)
    {
        display.clearDisplay();

        drawSleepingEyes();
        drawSleepingBrows();

        int wobble = (i % 6 < 3) ? 1 : -1;

        // Large Z
        drawSleepZ(
            110 - i / 2 + wobble,
            26 - i
        );

        // Medium Z
        if (i > 10)
        {
            drawSleepZ(
                110 - (i - 10) / 2 - wobble,
                26 - (i - 10)
            );
        }

        // Small Z
        if (i > 20)
        {
            drawSleepZ(
                110 - (i - 20) / 2 + wobble,
                26 - (i - 20)
            );
        }

        display.display();

        // 80 ms animation frame
        // while keeping audio running
        for (int t = 0; t < 8; t++)
        {
            audioLoop();
            delay(10);
        }
    }
}
void surprised()
{
    currentEmotion = SURPRISED;

    eyeOpen = EYE_RADIUS;
    pupilOffsetX = 0;
    pupilOffsetY = 0;

    drawFrame();

    // Bigger pupils
    display.fillCircle(
        LEFT_EYE_X + eyeoffsetx,
        LEFT_EYE_Y + eyeoffsety,
        7,
        SH110X_BLACK
    );

    display.fillCircle(
        RIGHT_EYE_X + eyeoffsetx,
        RIGHT_EYE_Y + eyeoffsety,
        7,
        SH110X_BLACK
    );

    display.display();
}

void angry()
{
    currentEmotion = ANGRY;

    // Fully open eyes
    eyeOpen = EYE_RADIUS;

    // Make Toki stare inward
    display.clearDisplay();

    // Draw normal eyes
    drawEye(LEFT_EYE_X, LEFT_EYE_Y);
    drawEye(RIGHT_EYE_X, RIGHT_EYE_Y);

    // Erase the normal pupils
    display.fillCircle(
        LEFT_EYE_X + eyeoffsetx + pupilOffsetX,
        LEFT_EYE_Y + eyeoffsety + pupilOffsetY,
        PUPIL_RADIUS,
        SH110X_WHITE
    );

    display.fillCircle(
        RIGHT_EYE_X + eyeoffsetx + pupilOffsetX,
        RIGHT_EYE_Y + eyeoffsety + pupilOffsetY,
        PUPIL_RADIUS,
        SH110X_WHITE
    );

    // Draw smaller pupils looking inward
    display.fillCircle(
        LEFT_EYE_X + eyeoffsetx + 2,
        LEFT_EYE_Y + eyeoffsety,
        3,
        SH110X_BLACK
    );

    display.fillCircle(
        RIGHT_EYE_X + eyeoffsetx - 2,
        RIGHT_EYE_Y + eyeoffsety,
        3,
        SH110X_BLACK
    );

    drawAngryBrows();

    display.display();
}

void curious()
{
    lookLeft();
    moveEyes();

    delay(500);

    lookCenter();
    moveEyes();
}

void neutral()
{
    currentEmotion = NEUTRAL;

    eyeOpen = EYE_RADIUS;

    eyeoffsetx = 0;
    eyeoffsety = 0;

    pupilOffsetX = 0;
    pupilOffsetY = 0;

    drawFrame();
}





// ===== Added Animation Helpers =====
void happyBounce()
{
    for (int i = 0; i < 2; i++)
    {
        eyeoffsety = -3;
        happy();

        for (int t = 0; t < 7; t++)
        {
            audioLoop();
            delay(10);
        }

        eyeoffsety = -1;
        happy();

        for (int t = 0; t < 7; t++)
        {
            audioLoop();
            delay(10);
        }
    }

    eyeoffsety = -2;
    happy();
}
void angryShake()
{
    for (int i = 0; i < 3; i++)
    {
        eyeoffsetx = -1;
        angry();

        for (int t = 0; t < 4; t++)
        {
            audioLoop();
            delay(10);
        }

        eyeoffsetx = 1;
        angry();

        for (int t = 0; t < 4; t++)
        {
            audioLoop();
            delay(10);
        }
    }

    eyeoffsetx = 0;
    angry();
}

void curiousAnimation()
{
    currentEmotion = CURIOUS;

    lookLeft();
    moveEyes();

    for (int t = 0; t < 25; t++)
    {
        audioLoop();
        delay(10);
    }

    lookUp();
    moveEyes();

    for (int t = 0; t < 25; t++)
    {
        audioLoop();
        delay(10);
    }

    lookRight();
    moveEyes();

    for (int t = 0; t < 25; t++)
    {
        audioLoop();
        delay(10);
    }

    currentEmotion = NEUTRAL;

    lookCenter();
    moveEyes();
}

void surpriseAnimation()
{
    surprised();

    // 400 ms while keeping audio alive
    for (int t = 0; t < 40; t++)
    {
        audioLoop();
        delay(10);
    }

    blink();

    // 150 ms while keeping audio alive
    for (int t = 0; t < 15; t++)
    {
        audioLoop();
        delay(10);
    }

    surprised();
}

void sleepBreathing()
{
    for (int i = 0; i < 5; i++)
    {
        eyeoffsety = 0;

        display.clearDisplay();
        drawSleepingEyes();
        drawSleepingBrows();
        display.display();

        for (int t = 0; t < 50; t++)
        {
            audioLoop();
            delay(10);
        }

        eyeoffsety = 1;

        display.clearDisplay();
        drawSleepingEyes();
        drawSleepingBrows();
        display.display();

        for (int t = 0; t < 50; t++)
        {
            audioLoop();
            delay(10);
        }
    }
}

void idleAnimation(){
  currentEmotion = NEUTRAL;
  blink();
  delay(300);
  lookLeft(); moveEyes(); delay(500);
  lookCenter(); moveEyes(); delay(500);
  lookRight(); moveEyes(); delay(500);
  lookCenter(); moveEyes();
  blink();
}

void startup()
{
    eyeOpen = 0;

    // Eyes opening
    for (int i = 0; i < EYE_RADIUS; i++)
    {
        eyeOpen++;

        drawFrame();

        // Keep audio flowing
        for (int t = 0; t < 4; t++)
        {
            audioLoop();
            delay(10);
        }
    }

    // Blink
    blink();

    // Look left
    lookLeft();
    moveEyes();

    // Keep audio alive
    for (int t = 0; t < 20; t++)
    {
        audioLoop();
        delay(10);
    }

    // Look right
    lookRight();
    moveEyes();

    for (int t = 0; t < 20; t++)
    {
        audioLoop();
        delay(10);
    }

    // Look center
    lookCenter();
    moveEyes();

    for (int t = 0; t < 20; t++)
    {
        audioLoop();
        delay(10);
    }

    // Final blink
    blink();

    // Let audio finish naturally
    while (wav && wav->isRunning())
    {
        audioLoop();
    }
}
void playSound(const char *filename)
{
    // Stop anything currently playing
    if (wav && wav->isRunning())
    {
        wav->stop();
    }

    delete wav;
    delete audioFile;

    wav = nullptr;
    audioFile = nullptr;

    audioFile = new AudioFileSourceLittleFS(filename);

    if (!audioFile->isOpen())
    {
        Serial.print("Audio not found: ");
        Serial.println(filename);
        return;
    }

    wav = new AudioGeneratorWAV();

    if (!wav->begin(audioFile, audioOutput))
    {
        Serial.println("WAV playback failed");

        delete wav;
        delete audioFile;

        wav = nullptr;
        audioFile = nullptr;

        return;
    }

    Serial.print("Playing: ");
    Serial.println(filename);
}

void audioLoop()
{
    if (wav && wav->isRunning())
    {
        if (!wav->loop())
        {
            wav->stop();

            delete wav;
            delete audioFile;

            wav = nullptr;
            audioFile = nullptr;

            Serial.println("Audio finished");
        }
    }
}



