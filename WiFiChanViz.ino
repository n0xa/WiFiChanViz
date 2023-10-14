#include <WiFi.h>
#include <ESP32-Chimera-Core.h> // https://github.com/tobozo/ESP32-Chimera-Core

// just some over-engineered demoscene fonts for LGFX, ripped and converted to
// sprites from https://opengameart.org/content/new-original-grafx2-font-collection
#include "ImgFont.h"
#include "FontCursive.h"
#include "FontSaikyoBlack.h"
#include "fx.h"

LGFX &tft( M5.Lcd );
LGFX_Sprite *spriteMain = nullptr;
LGFX_Sprite *spriteMask = nullptr;
LGFX_Sprite *fontSprite = nullptr;

bool swapped = false; // sprite-swapping
bool units_rendered = false;
uint32_t scanChanDelay = 150; // default


//#define USE_SCREENSHOTS
#define channels_count 13

#define SPRITE_WIDTH   120 // calibrated to work without psram
#define SPRITE_HEIGHT  180  // calibrated to work without psram

#define GRAPH_HEIGHT   150 // smaller than sprite height, leave some pixels for perspective
#define GRAPH_PXOFF_Y   2

const float invX          = 1.0-(1.5/float(GRAPH_HEIGHT));
const float invY          = 1.0-(1.5/float(SPRITE_WIDTH));
const float barWidth      = SPRITE_WIDTH / (channels_count);
const uint32_t bottomY    = SPRITE_HEIGHT-1; // sprite bottom
size_t avgcount           = 0;

int32_t spritePosX, spritePosY; // where the graph will be rendered

uint32_t delayPosX,
         delayPosY,
         delayWidth = 110,
         delayHeight = 24,
         devCountPosX,
         devCountPosY,
         devCountWidth = 110,
         devCountHeight = 24,
         marginX = 0,
         marginY = 210
;

std::array<int,channels_count> totaldpc;

void drawUI()
{
  float grd = 0.0, incr = float(0x20)/float(tft.height());
  for( int y=0; y<tft.height(); y++ ) {
    grd+=incr;
    tft.drawGradientHLine( 0, y, tft.width(), (RGBColor){0x20,uint8_t(0x20+grd*2),0x20}, (RGBColor){uint8_t(0x80-grd*2),uint8_t(0xaa-grd),uint8_t(0x80-grd*2)} );
  }
  tft.drawRect( spritePosX-1, spritePosY-1, SPRITE_WIDTH+2, SPRITE_HEIGHT+2, (RGBColor){0xcc, 0xcc, 0xcc} );

  charDrawer_t charDrawer;
  // init sprite in native (8bit) color mode
  charDrawer.drawString( fontSprite, "?", -100, -100, &moor4bit, 0x000000U,  -1, 1.0 );

  devCountPosX = tft.width()-(devCountWidth+marginX);
  devCountPosY = tft.height()-(delayHeight+marginY);

  tft.setFont( &Font8x8C64 );
  tft.setTextDatum( TL_DATUM );
  tft.setTextColor( TFT_WHITE );
  for( int i=0; i<channels_count; i++ ) {
    tft.setCursor( spritePosX+barWidth*(i)+8, spritePosY+SPRITE_HEIGHT+tft.fontHeight() );
    tft.printf("%X", i+1 );
  }
}



void graphData( std::array<int,channels_count> data )
{
  swapped = !swapped;

  LGFX_Sprite *sprite1 = swapped ? spriteMain : spriteMask;
  LGFX_Sprite *sprite2 = swapped ? spriteMask : spriteMain;

  sprite1->clear();
  sprite2->pushRotateZoomWithAA( sprite1, SPRITE_WIDTH/2, (SPRITE_HEIGHT/2)-GRAPH_PXOFF_Y, 0, invX, invY );

  spriteFadeOut( sprite1, 234 );

  int minc = INT_MAX, maxc = 0;

  for( int i=0; i<data.size(); i++ ) {
    minc = min( minc, data[i] );
    maxc = max( maxc, data[i] );
  }

  float vdist = SPRITE_HEIGHT / maxc;

  for( int i=0; i<data.size(); i++ ) {
    int32_t x2 = barWidth*(i) + 6;
    int32_t h2 = map( data[i], minc, maxc, 0, GRAPH_HEIGHT );
    if( data[i] > 0 ) {
      for( int y=0;y<h2-1;y++ ) {
        uint16_t barcolor = getHeatMapColor( 0, bottomY, y, (RGBColor*)&heatMapColors, sizeof(heatMapColors)/sizeof(RGBColor) );
        sprite1->drawFastHLine( x2+1, bottomY-(y+1), barWidth-4, barcolor );
      }
      sprite1->drawRect( x2, bottomY-h2, barWidth-2, h2+3, (RGBColor){0x30,0x30,0x30} );
    } else {
      sprite1->drawFastHLine( x2, bottomY-1, barWidth-2, TFT_BLACK);
      sprite1->drawFastHLine( x2, bottomY,   barWidth-2, heatMapColors[0]);
    }

    totaldpc[i] += data[i];
  }

  sprite1->pushSprite( spritePosX, spritePosY );
  avgcount++;
}







void setup()
{
  M5.begin();

  tft.fillScreen( 0x000000U );

  spriteMain = new LGFX_Sprite( &tft );
  spriteMask = new LGFX_Sprite( &tft );
  fontSprite = new LGFX_Sprite( &tft );

  spriteMain->setColorDepth(16);
  spriteMask->setColorDepth(16);
  fontSprite->setColorDepth(1);

  #if defined USE_SCREENSHOTS
    M5.ScreenShot->begin();
  #endif

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  Serial.println("Setup done");

  totaldpc.fill(0);

  spritePosX = tft.width()/2-SPRITE_WIDTH/2;
  spritePosY = (tft.height()/2-SPRITE_HEIGHT/2) + 9;

  if( !spriteMain->createSprite( SPRITE_WIDTH, SPRITE_HEIGHT )
   || !spriteMask->createSprite( SPRITE_WIDTH, SPRITE_HEIGHT ) )
  {
    log_e("Failed to create sprite, halting");
    tft.print("Memory error");
    while(1) vTaskDelay(1);
  }

  drawUI();

}


uint32_t scanChannel = 0;
uint32_t lastscanstart = millis();


void loop()
{
  int n = WiFi.scanComplete();
  if (n == -2) {
    WiFi.scanNetworks (true, true, true, scanChanDelay );   //async, show_hidden
    lastscanstart = millis();
  } else if (n == -1) {
    M5.update();
    uint32_t guessChannel = (( millis()-lastscanstart )/scanChanDelay)+1;
    if( guessChannel > 0 && guessChannel < channels_count+1 && guessChannel != scanChannel ) {
      tft.setFont( &Font8x8C64 );
      tft.setTextDatum( TL_DATUM );
      tft.setTextColor( TFT_WHITE );
      tft.setCursor( spritePosX+barWidth*(scanChannel-1)+8, spritePosY+SPRITE_HEIGHT+tft.fontHeight() );
      tft.printf("%X", scanChannel );
      tft.setTextColor( TFT_RED );
      tft.setCursor( spritePosX+barWidth*(guessChannel-1)+8, spritePosY+SPRITE_HEIGHT+tft.fontHeight() );
      tft.printf("%X", guessChannel );
      scanChannel = guessChannel;
    }
    #if defined USE_SCREENSHOTS
      bool doscreenshot = false;
      if( M5.BtnC.wasPressed() ) doscreenshot = true;
      else if( Serial.available() ) {
        char c = Serial.read();
        if( c=='s' ) {
          doscreenshot = true;
        }
      }
      if( doscreenshot ) M5.ScreenShot->snap();
    #endif

    if( M5.BtnB.wasPressed() ) {
      if( scanChanDelay < 500 ) {
        scanChanDelay += 50;
      }
    }
    if( M5.BtnA.wasPressed() ) {
      if( scanChanDelay > 50 ) {
        scanChanDelay -= 50;
      }
    }
  } else if (n > 0) {
    std::array<int,channels_count> dpc;
    dpc.fill(0);
    if (n == 0) {
      Serial.println("no networks found");
    } else {
      for (int i = 0; i < n; ++i) {
        int32_t channel = WiFi.channel(i);
        dpc[channel]++;
      }
      graphData( dpc );

      int total = 0;
      /*
      for( int i=0; i<channels_count; i++ ) {
        Serial.printf(" %3d ", dpc[i] );
        total += dpc[i];
      }
      Serial.printf(" - total=%d, scanChanDelay=%dms)\n", total, scanChanDelay );
      */
    }
    drawCaption( String("Count: "+String(n)).c_str(), devCountPosX, devCountPosY, devCountWidth, devCountHeight );
    WiFi.scanDelete();
  }
}
