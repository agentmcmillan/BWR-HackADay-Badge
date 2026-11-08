# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import os, subprocess, tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "examples/esp-idf/app_video.c").read_text()
for header in [
    "bwr_badge_port.h",
    "esp_timer.h",
    "freertos/FreeRTOS.h",
    "freertos/semphr.h",
]:
    source = source.replace('#include "' + header + '"', "")
preamble = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_video.h"
#include "refresh_lab.h"
#include "saver_shapes.h"
#define PLANE 80640
#define TOP 48
#define BWR_BTN_PLUS 0
#define BWR_BTN_MINUS 1
#define BWR_BTN_A 2
#define BWR_BTN_B 3
#define portMAX_DELAY (-1)
#define configASSERT assert
typedef int SemaphoreHandle_t;
static bool locked,running;
static int xSemaphoreCreateMutex(void){return 1;}
static void xSemaphoreTake(int m,int t){(void)t;assert(m&&!locked);locked=true;}
static void xSemaphoreGive(int m){assert(m&&locked);locked=false;}
static uint8_t fb[PLANE],shown[PLANE];
static uint64_t now;
static unsigned idle_calls,draws,scenario,ends,hold_reads,wipes,text_calls;
static bool shortcut=true,closed;
static refresh_sample sample;
static int64_t esp_timer_get_time(void){return (int64_t)now;}
static void text(int x,int y,int scale,const char *s,bool ink){(void)ink;text_calls++;assert(x>=0&&y>=48&&x+(int)strlen(s)*8*scale<=960&&y+scale*8<=672);}
static void ui_keys(const char*a,const char*b,const char*c,const char*d){assert(a&&c&&d);(void)b;}
static void ui_frame(int pulls){assert(pulls==1&&!shortcut);draws++;sample=(refresh_sample){.changed=memcmp(fb,shown,PLANE)!=0,.actual_khz=20000,.pull_us=587000};now+=600000;memcpy(shown,fb,PLANE);}
static void ui_close(void){closed=true;}
static void ui_wipe(void){wipes++;memset(fb,0,sizeof fb);}
static void ui_refresh_shortcut(bool on){shortcut=on;}
static void ui_protect_overlay(const uint8_t *mask){assert(!mask);}
static void ui_protect_footer(bool on){assert(!on);}
bool ui_refresh_lab_begin(unsigned mhz){assert(mhz==4||mhz==20);return scenario!=2;}
void ui_refresh_lab_end(void){ends++;}
void ui_refresh_lab_sample(refresh_sample *out){*out=sample;}
static int btn_get(int timeout){
 if(scenario>=3&&timeout>0&&running){
  assert(!locked&&closed);now+=(uint64_t)timeout*1000;hold_reads++;
  if(hold_reads==3&&scenario==4)return BWR_BTN_B;
  if(hold_reads==3&&scenario==5)video_stop(true);
  if(hold_reads==3&&scenario==6)return BWR_BTN_PLUS;
  return -1;
 }
 if(timeout==100){idle_calls++;return idle_calls==1?BWR_BTN_A:BWR_BTN_B;}
 assert(timeout==0);
 if(scenario==1&&draws>=4)return BWR_BTN_B;
 return -1;
}
"""
checks = r"""
static void put(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
int main(void){
 video_init();assert(!video_request(20,1)&&!video_active());
 for(scenario=0;scenario<7;scenario++){
  bool native=scenario>=3;unsigned count=native?2:4,stride=native?74880:4680;
  size_t n=32+count*stride;uint8_t *data=calloc(1,n);assert(data);
  memcpy(data,native?"BWRINK1":"BWRVID1",8);
  if(native){data[8]=0xc0;data[9]=3;data[10]=0x70;data[11]=2;}else{data[8]=240;data[10]=156;}
  put(data+12,count);put(data+16,native?4000:1500);
  for(unsigned i=0;i<count;i++)memset(data+32+i*stride,i&1?0:255,stride);
  put(data+20,bwr_video_crc32(data+32,n-32));assert(video_accept(data,n));
  char identity[1024],expected_crc[64];video_status(identity,sizeof identity);
  snprintf(expected_crc,sizeof expected_crc,"\"clip_crc32\":%lu",(unsigned long)bwr_video_crc32(data+32,n-32));assert(strstr(identity,expected_crc));
  idle_calls=draws=ends=hold_reads=wipes=text_calls=0;now=0;shortcut=true;closed=false;
  app_video();assert(!video_active()&&!locked&&shortcut&&closed&&ends>=1);
  char status[1024];video_status(status,sizeof status);puts(status);
  assert(strstr(status,scenario==0?"\"calls\":4":scenario==1||scenario==3?"\"calls\":2":scenario==2?"\"calls\":0":"\"calls\":1"));
  if(native){assert(text_calls==0);if(scenario==3){assert(now>=9200000&&draws==2&&hold_reads>=80);}else assert(hold_reads==3);}
  if(scenario==6)assert(wipes==1);
 }
 video_stop(true);video_stop(false);assert(leave);
 free(clip_bytes);clip_bytes=NULL;
 puts("PASS video app: immutable owned clip, bounded playback, immediate Home cancellation, status/results and default-clock/shortcut cleanup");
}
"""
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d / "test.c").write_text(preamble + source + checks)
    subprocess.run(
        [
            os.environ.get("CC", "cc"),
            "-std=c11",
            "-O1",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            "-I",
            str(root / "components/bwr_display/include"),
            "-I",
            str(root / "examples/esp-idf"),
            str(d / "test.c"),
            str(root / "components/bwr_display/src/bwr_video.c"),
            str(root / "components/bwr_display/src/refresh_lab.c"),
            "-o",
            str(d / "test"),
        ],
        check=True,
    )
    subprocess.run([str(d / "test")], check=True)
