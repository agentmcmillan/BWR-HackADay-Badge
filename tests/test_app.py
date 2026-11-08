# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import os, subprocess, tempfile

root = Path(__file__).resolve().parents[1]
s = (root / "examples/esp-idf/app_refresh_lab.c").read_text()
for name in ("bwr_badge_port.h", "badge.h", "esp_attr.h", "esp_timer.h", "esp_log.h"):
    s = s.replace(f'#include "{name}"', "")
preamble = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "saver_shapes.h"
#include "refresh_lab.h"
#include "refresh_remote.h"
#define W 960
#define H 672
#define TOP 48
#define PLANE (W*H/8)
#define EXT_RAM_BSS_ATTR
#define BWR_BTN_PLUS 0
#define BTN_PLUS 0
#define BWR_BTN_MINUS 1
#define BTN_MINUS 1
#define BWR_BTN_A 2
#define BTN_A 2
#define BWR_BTN_B 3
#define BTN_B 3
#define ESP_LOGI(...) ((void)0)
static uint8_t fb[PLANE],shown[PLANE];
static int64_t now;
static unsigned scenario,phase,frames,reads,begins,ends,rate=4,part_n;
static bool shortcut=true,closed,cache_label,comparison;
static refresh_sample last;
static int64_t esp_timer_get_time(void){return now;}
static void ui_refresh_shortcut(bool on){shortcut=on;}
static void ui_protect_overlay(const uint8_t*m){assert(!m);}
static void ui_protect_footer(bool on){assert(!on);}
bool ui_refresh_lab_begin(unsigned mhz){assert(mhz==4||mhz==5||mhz==8||mhz==12||mhz==20);rate=mhz;begins++;part_n=0;return true;}
void ui_refresh_lab_end(void){ends++;rate=4;}
void ui_refresh_lab_sample(refresh_sample*out){reads++;*out=last;}
static void ui_close(void){closed=true;part_n=0;}
static void ui_keys(const char*p,const char*m,const char*a,const char*b){assert(p&&m&&a&&b);}
static void box(int x,int y,int w,int h,bool on){
 assert(x>=0&&y>=TOP&&x+w<=W&&y+h<=H);
 for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++){
  uint8_t bit=128u>>(xx&7);if(on)fb[yy*120+xx/8]|=bit;else fb[yy*120+xx/8]&=(uint8_t)~bit;
 }
}
static void outline(int x,int y,int w,int h,int t){box(x,y,w,t,true);box(x,y+h-t,w,t,true);box(x,y,t,h,true);box(x+w-t,y,t,h,true);}
static void text(int x,int y,int scale,const char*s,bool on){
 assert(x>=0&&x+(int)strlen(s)*scale*8<=W&&y>=TOP&&y+scale*8<=H);
 if(strstr(s,"not display FPS"))cache_label=true;
 if(strstr(s,"vs 4 MHz baseline:"))comparison=true;
 for(;*s;s++,x+=8*scale)box(x,y,scale*6,scale*6,on);
}
static void ui_frame(int n){
 assert(n==1&&!shortcut);frames++;
 bool changed=memcmp(shown,fb,PLANE)!=0;
 last=(refresh_sample){.changed=changed,.reopened=changed&&(!part_n||part_n==60),.actual_khz=rate==12?11428:rate*1000};
 if(changed){last.diff_us=100;last.write_us=5000;last.pull_us=475000;last.open_us=last.reopened?3000000:0;last.bytes=1024;last.transactions=4;part_n=last.reopened?1:part_n+1;}
 last.total_us=changed?last.diff_us+last.write_us+last.pull_us+last.open_us:2000;
 now+=(int64_t)last.total_us;memcpy(shown,fb,PLANE);
}
void refresh_remote_active(bool on){(void)on;}
bool refresh_remote_control(bool *leave){*leave=false;return false;}
bool refresh_remote_take(refresh_request *request){(void)request;return false;}
void refresh_remote_running(bool on){(void)on;}
void refresh_remote_finish(unsigned p,unsigned m,const refresh_result*r,uint64_t t){(void)p;(void)m;(void)r;(void)t;}
static int btn_get(int timeout){
 now+=(int64_t)timeout*1000;assert(frames<1000);
 if(scenario==6){
  if(!phase){phase=1;return BTN_A;}
  if(reads==60&&phase==1){phase=2;return BTN_MINUS;}
  if(phase==2){phase=3;return BTN_A;}
  if(reads==120)return BTN_B;
  return -1;
 }
 if(scenario==4)return BTN_B;
 if(scenario==1){
  if(phase<2){phase++;return BTN_PLUS;}
  if(phase==2){phase++;return BTN_MINUS;}
 }
 if(scenario==2){
  if(phase<4){phase++;return BTN_PLUS;}
  if(phase<8){phase++;return BTN_MINUS;}
 }
 if(scenario==5&&phase<5){phase++;return BTN_MINUS;}
 if(!reads){phase++;return BTN_A;}
 if(scenario==3&&reads>=7&&phase==1){phase=2;return BTN_A;}
 if(scenario==3&&phase==2)return BTN_B;
 if(reads>=LAB_FRAMES)return BTN_B;
 return -1;
}
"""
checks = r"""
int main(void){
 refresh_result r={0};refresh_sample a={.changed=true,.pull_us=475000,.actual_khz=8000};
 refresh_result_add(&r,&a,500000,500);assert(r.calls==1&&r.pulls==1&&r.steady==1&&r.best_us==500000);
 a.reopened=true;refresh_result_add(&r,&a,3500000,200);assert(r.reopens==1&&r.worst_us==3500000&&r.steady==1);
 a=(refresh_sample){0};refresh_result_add(&r,&a,2000,10);assert(r.calls==3&&r.pulls==2&&r.best_us==500000);
 for(scenario=0;scenario<7;scenario++){
  phase=frames=reads=begins=ends=0;now=0;closed=cache_label=comparison=false;shortcut=true;memset(shown,0,sizeof shown);
  app_refresh_lab();assert(ends==1&&rate==4&&shortcut&&closed);
  if(scenario==6)assert(reads==120&&comparison&&begins==2);else if(scenario==4)assert(reads==0);else if(scenario==3)assert(reads==7);else assert(reads==60);
  if(scenario==1)assert(begins==2);
  if(scenario==2)assert(cache_label&&begins==5);
  if(scenario==5)assert(begins==6);
 }
 puts("PASS Refresh Lab: finite 60-frame bursts, all pattern/clock selections, cache labeling, early stop/Home, text/geometry bounds, statistics and 4MHz/shortcut restoration");
}
"""
with tempfile.TemporaryDirectory() as d:
    p = Path(d)
    (p / "test.c").write_text(preamble + s + checks)
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
            str(p / "test.c"),
            str(root / "components/bwr_display/src/refresh_lab.c"),
            "-o",
            str(p / "test"),
        ],
        check=True,
    )
    subprocess.run([str(p / "test")], check=True)
