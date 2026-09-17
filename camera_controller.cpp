#include "camera_controller.h"
#include "log.h"
#include <windows.h>
#include <commctrl.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>

#pragma comment(lib, "Comctl32.lib")

namespace {
constexpr float PI=3.14159265358979323846f, TWO_PI=PI*2.0f, DEG=PI/180.0f;
constexpr int ID_LOAD_BASE=1000, ID_SAVE_BASE=1100, ID_APPLY=1200, ID_CAPTURE=1201;
constexpr int ID_RESET_GAME=1202, ID_DEFAULT=1203, ID_SYNC=1204;
constexpr int ID_SENS=1300;
constexpr int ID_SLIDER_BASE=1400;
constexpr int ID_STATUS=1500;
constexpr UINT_PTR UI_TIMER=1;
static bool down(int vk){return (GetAsyncKeyState(vk)&0x8000)!=0;}
static bool pressed_once(int vk){ static bool prev[256]{}; bool d=down(vk), old=prev[vk]; prev[vk]=d; return d&&!old; }
static float clampf(float v,float lo,float hi){return std::max(lo,std::min(hi,v));}
static float wrap(float v){while(v>PI)v-=TWO_PI;while(v<-PI)v+=TWO_PI;return v;}
static float len3(Vec3 v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}

static void derive_angles(const CameraPose&p,float&yaw,float&pitch){Vec3 d{p.interest.x-p.position.x,p.interest.y-p.position.y,p.interest.z-p.position.z};float l=len3(d);if(l<.0001f)l=1; yaw=std::atan2(d.x,d.z);pitch=std::asin(clampf(d.y/l,-.9999f,.9999f));}
static void set_edit(HWND h,float v){char b[64];std::snprintf(b,sizeof(b),"%.6f",v);SetWindowTextA(h,b);}
static float read_edit(HWND h,float f){char b[64]{};GetWindowTextA(h,b,sizeof(b));char*e=nullptr;float v=std::strtof(b,&e);return(e==b)?f:v;}
static HWND make_control(HWND parent,LPCWSTR cls,LPCWSTR text,DWORD style,int x,int y,int w,int h,int id,HINSTANCE inst){ return CreateWindowExW(0,cls,text,style,x,y,w,h,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),inst,nullptr); }
static void label(HWND p,const char*t,int x,int y,int w=145){ wchar_t wt[512]{}; MultiByteToWideChar(CP_ACP,0,t,-1,wt,512); make_control(p,L"STATIC",wt,WS_CHILD|WS_VISIBLE,x,y,w,20,0,GetModuleHandleW(nullptr)); }
static void set_slider(HWND h,int v){if(h)SendMessageA(h,TBM_SETPOS,TRUE,v);}
static int slider(HWND h){return h?(int)SendMessageA(h,TBM_GETPOS,0,0):0;}
static std::wstring dll_dir(){HMODULE hm=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&dll_dir),&hm);wchar_t path[MAX_PATH]{};GetModuleFileNameW(hm,path,MAX_PATH);std::wstring s(path);size_t p=s.find_last_of(L"\\/");return p==std::wstring::npos?L".":s.substr(0,p);}
static std::wstring save_path(int slot){return dll_dir()+L"\\save\\save0"+std::to_wstring(slot)+L".txt";}

LRESULT CALLBACK UiProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    auto*self=reinterpret_cast<FreeCameraController*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){auto*cs=reinterpret_cast<CREATESTRUCTW*>(lp);self=reinterpret_cast<FreeCameraController*>(cs->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)self);}
    if(self){
        if(msg==WM_TIMER && wp==UI_TIMER){ self->refresh_ui_live(); return 0; }
        if(msg==WM_HSCROLL){
            HWND src=reinterpret_cast<HWND>(lp);
            if(src==self->m_sensitivity_slider){
                self->m_sensitivity=clampf((float)slider(src)/100.0f,0.10f,3.0f);
                char b[64];std::snprintf(b,sizeof(b),"Sensitivity: %.0f%%",self->m_sensitivity*100.0f);SetWindowTextA(self->m_sensitivity_label,b);
                return 0;
            }
            for(int i=0;i<6;i++) if(src==self->m_sliders[i]) { if(self->m_sync_fov_near && (i==3 || i==4)){ int v=slider(src); set_slider(self->m_sliders[i==3?4:3],v); self->apply_slider(i); self->apply_slider(i==3?4:3); } else self->apply_slider(i); return 0; }
            return 0;
        }
        if(msg==WM_COMMAND){int id=LOWORD(wp);if(id>=ID_LOAD_BASE&&id<ID_LOAD_BASE+9){self->load_slot(id-ID_LOAD_BASE+1);return 0;}if(id>=ID_SAVE_BASE&&id<ID_SAVE_BASE+9){self->save_slot(id-ID_SAVE_BASE+1);return 0;}if(id==ID_APPLY){self->apply_ui();return 0;}if(id==ID_CAPTURE){self->refresh_ui();return 0;}if(id==ID_RESET_GAME){self->reset_to_game_position();return 0;}if(id==ID_DEFAULT){self->default_position();return 0;}if(id==ID_SYNC){self->m_sync_fov_near=(IsDlgButtonChecked(hwnd,ID_SYNC)==BST_CHECKED);if(self->m_sync_fov_near){int v=slider(self->m_sliders[3]);set_slider(self->m_sliders[4],v);self->apply_slider(3);self->apply_slider(4);}return 0;}}
        if(msg==WM_SIZE){
            int w=LOWORD(lp), h=HIWORD(lp);
            if(w<760) w=760; if(h<700) h=700;
            const int margin=15, gap=25, colw=(w-2*margin-gap)/2, editw=std::max(150,colw-215), sliderw=std::max(120,colw-editw-15);
            // Position (0..2) and Interest (3..5), each in its own column.
            for(int i=0;i<6;i++) if(self->m_edits[i]){
                int col=i/3,row=i%3; int x=margin+col*(colw+gap), y=15+row*58;
                SetWindowPos(self->m_edits[i],nullptr,x,y+22,editw,24,SWP_NOZORDER|SWP_NOACTIVATE);
            }
            // Yaw/Pitch/Roll in the left column, FOV/Near in the right column.
            for(int i=6;i<11;i++) if(self->m_edits[i]){
                int j=i-6,col=j/3,row=j%3; int x=margin+col*(colw+gap), y=205+row*58;
                SetWindowPos(self->m_edits[i],nullptr,x,y+22,editw,24,SWP_NOZORDER|SWP_NOACTIVATE);
                if(self->m_sliders[i-6]) SetWindowPos(self->m_sliders[i-6],nullptr,x+editw+15,y+20,sliderw,28,SWP_NOZORDER|SWP_NOACTIVATE);
            }
            if(self->m_focal_edit) SetWindowPos(self->m_focal_edit,nullptr,margin+colw+gap,379,editw,24,SWP_NOZORDER|SWP_NOACTIVATE);
            if(self->m_sliders[5]) SetWindowPos(self->m_sliders[5],nullptr,margin+colw+gap+editw+15,399,sliderw,28,SWP_NOZORDER|SWP_NOACTIVATE);
            if(self->m_sensitivity_slider) SetWindowPos(self->m_sensitivity_slider,nullptr,margin,477,std::max(250,w-145),28,SWP_NOZORDER|SWP_NOACTIVATE);
            if(self->m_sensitivity_label) SetWindowPos(self->m_sensitivity_label,nullptr,w-120,477,100,24,SWP_NOZORDER|SWP_NOACTIVATE);
            if(self->m_status) SetWindowPos(self->m_status,nullptr,margin,h-45,std::max(300,w-30),25,SWP_NOZORDER|SWP_NOACTIVATE);
            return 0;
        }
        if(msg==WM_CLOSE){ShowWindow(hwnd,SW_HIDE);return 0;}
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}

void FreeCameraController::initialize(MegaMixCameraBridge*bridge){m_bridge=bridge;Log::write("FreeCameraController v35: QoL hotkeys + FOV/Near sync initialized");}
bool FreeCameraController::down(int vk){return ::down(vk);}

void FreeCameraController::toggle(){
 if(!m_bridge||!m_bridge->valid())return;
 if(!m_bridge->enabled()){
   m_game_window=GetForegroundWindow();
   m_game_roll_capture=0.0f;
   m_bridge->get_current_game_roll(m_game_roll_capture);
   m_enabled=true;
   m_bridge->set_enabled(true);
 } else {
   m_enabled=false;
   m_bridge->set_enabled(false);
 }
 if(m_enabled){
   m_input_locked=false;m_mouse_locked=false;m_all_locked=false;m_have_center=false;m_bridge->read(m_home);derive_angles(m_home,m_yaw,m_pitch);
   m_roll=m_game_roll_capture;
   m_bridge->set_roll(m_roll);
   m_focal_distance=std::max(.05f,len3(Vec3{m_home.interest.x-m_home.position.x,m_home.interest.y-m_home.position.y,m_home.interest.z-m_home.position.z}));
   m_home.rot_y=m_roll;
   // Deliberately do NOT open the UI on F8. F10 is the only UI toggle.
   Log::write("FreeCameraController: FREECAM ON (UI remains hidden)");
 } else {m_have_center=false;close_ui();Log::write("FreeCameraController: FREECAM OFF");}
}

void FreeCameraController::open_ui(){
 if(m_ui){ShowWindow(m_ui,SW_SHOW);refresh_ui();return;}
 INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_BAR_CLASSES};InitCommonControlsEx(&ic);
 WNDCLASSW wc{};wc.lpfnWndProc=UiProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"MegaMixFreeCamControlV27";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);
 m_ui=CreateWindowExW(WS_EX_TOOLWINDOW,L"MegaMixFreeCamControlV27",L"MegaMix+ Free Camera Controller",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_VISIBLE,80,60,900,760,m_game_window,nullptr,wc.hInstance,this);
 // Two clean columns: position/interest first, then orientation/settings.
 // Every slider is placed beside the value it actually controls.
 const char* names[]={
     "Position X   [A / D]","Position Y   [Space / Ctrl]","Position Z   [W / S]",
     "Interest X","Interest Y","Interest Z",
     "Yaw (degrees)   [Mouse X]","Pitch (degrees) [Mouse Y]","Roll (degrees)  [Q / E]",
     "FOV (degrees)   [R / F]","Near Clip"
 };
 const int row_h=58, left=15, right=455, edit_w=190, slider_w=190;
 // Position and Interest: plain numeric fields.
 for(int i=0;i<6;i++){
     int col=i/3, row=i%3;
     int x=(col==0)?left:right, y=15+row*row_h;
     label(m_ui,names[i],x,y,edit_w+slider_w);
     m_edits[i]=make_control(m_ui,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,x,y+22,edit_w,24,2000+i,wc.hInstance);
 }
 // Orientation/settings: numeric edit + matching slider in the same row.
 for(int i=6;i<11;i++){
     int j=i-6, col=j/3, row=j%3;
     int x=(col==0)?left:right, y=205+row*58;
     label(m_ui,names[i],x,y,edit_w+slider_w);
     m_edits[i]=make_control(m_ui,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,x,y+22,edit_w,24,2000+i,wc.hInstance);
     int sx=x+205;
     if(i==10){
         // Near is the fifth slider; it belongs directly beside Near Clip.
         m_sliders[4]=make_control(m_ui,TRACKBAR_CLASSW,L"",WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS,sx,y+20,slider_w,28,ID_SLIDER_BASE+4,wc.hInstance);
     } else {
         int si=i-6;
         m_sliders[si]=make_control(m_ui,TRACKBAR_CLASSW,L"",WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS,sx,y+20,slider_w,28,ID_SLIDER_BASE+si,wc.hInstance);
     }
 }
 // Focal distance gets its own matching edit + slider on the right of the bottom settings row.
 int fy=379;
 label(m_ui,"Focal distance   ([ + / ])",right,fy,edit_w+slider_w);
 m_focal_edit=make_control(m_ui,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,right,fy+22,edit_w,24,2011,wc.hInstance);
 m_sliders[5]=make_control(m_ui,TRACKBAR_CLASSW,L"",WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS,right+205,fy+20,slider_w,28,ID_SLIDER_BASE+5,wc.hInstance);
 // Sensitivity is independent from camera properties, so keep it in its own full-width row.
 int sy=455;
 label(m_ui,"Master sensitivity  (Alt + movement = 20%)",left,sy,300);
 m_sensitivity_slider=make_control(m_ui,TRACKBAR_CLASSW,L"",WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS,left,sy+22,520,28,ID_SENS,wc.hInstance);
 m_sensitivity_label=make_control(m_ui,L"STATIC",L"100%",WS_CHILD|WS_VISIBLE,left+530,sy+22,80,24,0,wc.hInstance);
 label(m_ui,"EDIT ANY VALUE BELOW, THEN PRESS Apply. Position/Interest are world coordinates; Yaw/Pitch/Focal control the viewing direction when Interest is unchanged.",left,500,850);
 make_control(m_ui,L"BUTTON",L"Apply All Values (Alt+U)",WS_CHILD|WS_VISIBLE,left,530,135,30,ID_APPLY,wc.hInstance);
 make_control(m_ui,L"BUTTON",L"Refresh / Capture",WS_CHILD|WS_VISIBLE,160,530,135,30,ID_CAPTURE,wc.hInstance);
 make_control(m_ui,L"BUTTON",L"Get Game Camera (G)",WS_CHILD|WS_VISIBLE,315,530,150,30,ID_RESET_GAME,wc.hInstance);
 make_control(m_ui,L"BUTTON",L"Default Camera (H)",WS_CHILD|WS_VISIBLE,475,530,140,30,ID_DEFAULT,wc.hInstance);
 make_control(m_ui,L"BUTTON",L"Sync FOV + Near Clip",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,625,530,190,30,ID_SYNC,wc.hInstance);
 label(m_ui,"LOAD SLOT  [1-9]",left,580,150);label(m_ui,"SAVE SLOT  [Alt+1-9]",right,580,150);
 for(int i=0;i<9;i++){int col=i%5,row=i/5;int x=left+col*70,yy=605+row*32;char b[8];std::snprintf(b,sizeof(b),"%d",i+1);wchar_t wb[8]{};MultiByteToWideChar(CP_ACP,0,b,-1,wb,8);make_control(m_ui,L"BUTTON",wb,WS_CHILD|WS_VISIBLE,x,yy,60,27,ID_LOAD_BASE+i,wc.hInstance);make_control(m_ui,L"BUTTON",wb,WS_CHILD|WS_VISIBLE,right+col*70,yy,60,27,ID_SAVE_BASE+i,wc.hInstance);}
 m_status=make_control(m_ui,L"STATIC",L"",WS_CHILD|WS_VISIBLE,left,680,850,35,ID_STATUS,wc.hInstance);
 SetTimer(m_ui,UI_TIMER,100,nullptr);
 set_slider(m_sensitivity_slider,100);
 for(int i=0;i<6;i++)set_slider(m_sliders[i],50);
 // Correct slider ranges: yaw/roll are -180..180, pitch is -90..90, FOV is 1..179,
 // near is logarithmic 1e-4..1, focal is 0..20.
 SendMessageA(m_sliders[0],TBM_SETRANGE,TRUE,MAKELONG(0,100));
 SendMessageA(m_sliders[1],TBM_SETRANGE,TRUE,MAKELONG(0,100));
 SendMessageA(m_sliders[2],TBM_SETRANGE,TRUE,MAKELONG(0,100));
 SendMessageA(m_sliders[3],TBM_SETRANGE,TRUE,MAKELONG(0,100));
 SendMessageA(m_sliders[4],TBM_SETRANGE,TRUE,MAKELONG(0,100));
 SendMessageA(m_sliders[5],TBM_SETRANGE,TRUE,MAKELONG(0,100));
 refresh_ui();
}
void FreeCameraController::close_ui(){if(m_ui){KillTimer(m_ui,UI_TIMER);ShowWindow(m_ui,SW_HIDE);}}

void FreeCameraController::refresh_ui_live(){
 if(!m_ui||!IsWindowVisible(m_ui)||!m_bridge||!m_bridge->enabled())return;
 HWND focus=GetFocus();
 // Never overwrite a field while the user is actively editing it. The next
 // timer tick after focus leaves the edit will synchronize it to the camera.
 for(int i=0;i<11;i++) if(focus==m_edits[i]) return;
 if(focus==m_focal_edit) return;
 refresh_ui();
}

void FreeCameraController::refresh_ui(){
 if(!m_ui||!m_bridge||!m_bridge->enabled())return;
 CameraPose p{};if(!m_bridge->read(p))return;
 float live_yaw=0,live_pitch=0; derive_angles(p,live_yaw,live_pitch);
 float live_focal=std::max(.001f,len3(Vec3{p.interest.x-p.position.x,p.interest.y-p.position.y,p.interest.z-p.position.z}));
 // The roll offset is the actual freecam roll currently applied to the A3DA
 // up vector; do not invent a value from the legacy rotation state.
 m_yaw=(live_yaw>=PI-1e-5f)?-PI:live_yaw; m_pitch=live_pitch; m_focal_distance=live_focal;
 float v[11]={p.position.x,p.position.y,p.position.z,p.interest.x,p.interest.y,p.interest.z,m_yaw/DEG,m_pitch/DEG,m_roll/DEG,p.perspective,p.near_clip};
 for(int i=0;i<11;i++)set_edit(m_edits[i],v[i]);
 set_edit(m_focal_edit,m_focal_distance);
 set_slider(m_sliders[0],(int)clampf((m_yaw/DEG+180)/360*100,0,100));
 set_slider(m_sliders[1],(int)clampf((live_pitch/DEG+90)/180*100,0,100));
 set_slider(m_sliders[2],(int)clampf((m_roll/DEG+180)/360*100,0,100));
 set_slider(m_sliders[3],(int)clampf((p.perspective-1)/178*100,0,100));
 set_slider(m_sliders[4],(int)clampf((std::log10(std::max(.0001f,p.near_clip))+4)*25,0,100));
 set_slider(m_sliders[5],(int)clampf(m_focal_distance/20.0f*100,0,100));
 set_slider(m_sensitivity_slider,(int)clampf(m_sensitivity*100,10,300));
 char s[256];
 std::snprintf(s,sizeof(s),"LIVE CAMERA  |  POS %.3f, %.3f, %.3f  |  FOV %.3f  |  Near %.5f  |  Roll %.2f deg",p.position.x,p.position.y,p.position.z,p.perspective,p.near_clip,m_roll/DEG);
 if(m_status)SetWindowTextA(m_status,s);
}

void FreeCameraController::apply_slider(int idx){
 if(m_all_locked||!m_bridge||!m_bridge->enabled())return;
 int pos=slider(m_sliders[idx]);
 CameraPose p{}; if(!m_bridge->read(p))return;
 if(idx==0)m_yaw=((float)pos/100.0f*360.0f-180.0f)*DEG;
 else if(idx==1)m_pitch=((float)pos/100.0f*180.0f-90.0f)*DEG;
 else if(idx==2){m_roll=((float)pos/100.0f*360.0f-180.0f)*DEG;}
 else if(idx==3)p.perspective=1.0f+(float)pos/100.0f*178.0f;
 else if(idx==4){float t=(float)pos/100.0f;m_bridge->write(p);p.near_clip=std::pow(10.0f,-4.0f+4.0f*t);}
 else if(idx==5)m_focal_distance=std::max(.05f,(float)pos/100.0f*20.0f);
 apply_orientation(p); if(idx==2)m_bridge->set_roll(m_roll); m_bridge->write(p);
 if(idx==0)set_edit(m_edits[6],m_yaw/DEG); else if(idx==1)set_edit(m_edits[7],m_pitch/DEG); else if(idx==2){set_edit(m_edits[8],m_roll/DEG);} else if(idx==3)set_edit(m_edits[9],p.perspective); else if(idx==4)set_edit(m_edits[10],p.near_clip); else if(idx==5)set_edit(m_focal_edit,m_focal_distance);
}

void FreeCameraController::apply_orientation(CameraPose&pose){float cp=std::cos(m_pitch);float cpitch=std::sin(m_pitch);Vec3 dir{std::sin(m_yaw)*cp,cpitch,std::cos(m_yaw)*cp};pose.interest={pose.position.x+dir.x*m_focal_distance,pose.position.y+dir.y*m_focal_distance,pose.position.z+dir.z*m_focal_distance};}
void FreeCameraController::apply_ui(){
 if(m_all_locked||!m_bridge||!m_bridge->enabled())return;CameraPose p{};if(!m_bridge->read(p))return;
 p.position.x=read_edit(m_edits[0],p.position.x);p.position.y=read_edit(m_edits[1],p.position.y);p.position.z=read_edit(m_edits[2],p.position.z);
 float entered_ix=read_edit(m_edits[3],p.interest.x), entered_iy=read_edit(m_edits[4],p.interest.y), entered_iz=read_edit(m_edits[5],p.interest.z);
 float live_yaw=0,live_pitch=0; derive_angles(p,live_yaw,live_pitch);
 float entered_yaw=read_edit(m_edits[6],live_yaw/DEG)*DEG, entered_pitch=clampf(read_edit(m_edits[7],live_pitch/DEG)*DEG,-89*DEG,89*DEG);
 bool interest_changed=std::fabs(entered_ix-p.interest.x)>1e-5f||std::fabs(entered_iy-p.interest.y)>1e-5f||std::fabs(entered_iz-p.interest.z)>1e-5f;
 if(interest_changed){p.interest={entered_ix,entered_iy,entered_iz};derive_angles(p,m_yaw,m_pitch);m_focal_distance=std::max(.05f,len3(Vec3{p.interest.x-p.position.x,p.interest.y-p.position.y,p.interest.z-p.position.z}));}
 else {m_yaw=entered_yaw;m_pitch=entered_pitch;m_focal_distance=std::max(.05f,read_edit(m_focal_edit,m_focal_distance));apply_orientation(p);}
 m_roll=read_edit(m_edits[8],m_roll/DEG)*DEG;p.perspective=clampf(read_edit(m_edits[9],p.perspective),1,179);p.near_clip=std::max(.0001f,read_edit(m_edits[10],p.near_clip));m_bridge->set_roll(m_roll);m_bridge->write(p);refresh_ui();}

void FreeCameraController::default_position(){
 if(m_all_locked||!m_bridge||!m_bridge->enabled())return;
 CameraPose p{}; m_bridge->read(p);
 p.position={0.0f,1.0f,3.45000005f};
 p.interest={0.0f,1.0f,0.0f};
 m_yaw=-PI; m_pitch=0.0f; m_roll=0.0f; m_focal_distance=3.45000005f;
 p.perspective=32.2673416f; p.near_clip=0.0500000007f;
  m_bridge->set_roll(m_roll); m_bridge->write(p); refresh_ui();
}
void FreeCameraController::reset_to_game_position(){
 if(m_all_locked||!m_bridge||!m_bridge->enabled())return;
 m_bridge->set_enabled(false);
 CameraPose p{};
 bool ok=m_bridge->read(p);
 float game_roll=0.0f; m_bridge->get_current_game_roll(game_roll);
 m_bridge->set_enabled(true);
 if(!ok)return;
 derive_angles(p,m_yaw,m_pitch);
 m_roll=game_roll;
 m_focal_distance=std::max(.05f,len3(Vec3{p.interest.x-p.position.x,p.interest.y-p.position.y,p.interest.z-p.position.z}));
 m_bridge->set_roll(m_roll);
 m_bridge->write(p);
 refresh_ui();
 Log::write("FreeCameraController: reset to current game camera state (including roll/FOV)");
}
void FreeCameraController::reset_pose(){if(m_all_locked||!m_bridge||!m_bridge->enabled())return;CameraPose p=m_home;derive_angles(m_home,m_yaw,m_pitch);m_roll=m_home.rot_y;m_focal_distance=std::max(.05f,len3(Vec3{m_home.interest.x-m_home.position.x,m_home.interest.y-m_home.position.y,m_home.interest.z-m_home.position.z}));m_bridge->set_roll(m_roll);m_bridge->write(p);refresh_ui();}

bool FreeCameraController::game_input_available(){
 if(!m_game_window || !IsWindow(m_game_window) || !IsWindowVisible(m_game_window)) return false;
 if(GetForegroundWindow()!=m_game_window) return false;
 RECT rc{}; if(!GetClientRect(m_game_window,&rc)) return false;
 POINT p{}; if(!GetCursorPos(&p)) return false;
 if(!ScreenToClient(m_game_window,&p)) return false;
 if(p.x<0 || p.y<0 || p.x>=rc.right || p.y>=rc.bottom) return false;
 return true;
}

void FreeCameraController::update_mouse(CameraPose&pose){
 if(m_mouse_locked || !game_input_available()) { m_have_center=false; return; }
 RECT rc{}; if(!GetClientRect(m_game_window,&rc)){m_have_center=false;return;}
 POINT c{rc.right/2,rc.bottom/2}; ClientToScreen(m_game_window,&c);
 if(!m_have_center){m_center=c;SetCursorPos(c.x,c.y);m_have_center=true;return;}
 POINT p{};if(!GetCursorPos(&p)){m_have_center=false;return;}
 float dx=(float)(p.x-m_center.x),dy=(float)(p.y-m_center.y);
 if(dx==0&&dy==0)return;
 float s=.0030f*m_sensitivity;m_yaw=wrap(m_yaw-dx*s);m_pitch=clampf(m_pitch-dy*s,-1.55f,1.55f);apply_orientation(pose);SetCursorPos(c.x,c.y);
}

void FreeCameraController::update_roll(CameraPose&pose){
 if(!m_bridge || m_all_locked || !game_input_available()) return;
 bool q=down('Q'), e=down('E');
 const float step=(down(VK_SHIFT)?90.0f:1.0f)*DEG*m_sensitivity;
 if(down(VK_SHIFT)){
   if(pressed_once('Q'))m_roll=wrap(m_roll+90.0f*DEG);
   else if(pressed_once('E'))m_roll=wrap(m_roll-90.0f*DEG);
 } else {
   if(q)m_roll+=step;
   if(e)m_roll-=step;
   m_roll=wrap(m_roll);
 }
 // Roll is stored in freecam state and applied to the existing engine up-vector
 // state by the bridge. Reapply it every frame so a native A3DA up-vector update
 // cannot erase the current freecam roll. Do not feed it through the rotation getter.
 m_bridge->set_roll(m_roll);
 pose.rot_y=m_roll;
}

void FreeCameraController::update_focal(CameraPose&pose){float fs=(down(VK_SHIFT)?0.20f:0.05f)*m_sensitivity;if(down(VK_OEM_4))m_focal_distance=std::max(.05f,m_focal_distance-fs);if(down(VK_OEM_6))m_focal_distance+=fs;apply_orientation(pose);}
void FreeCameraController::update_fov(CameraPose&pose){float fs=(down(VK_SHIFT)?2.0f:.25f)*m_sensitivity;if(down('R'))pose.perspective=clampf(pose.perspective-fs,1,179);if(down('F'))pose.perspective=clampf(pose.perspective+fs,1,179);}

void FreeCameraController::update_freecam(){
 if(!m_bridge||m_all_locked)return;
 // The game must own the foreground window, but FOV/focal controls should not
 // depend on the mouse being inside the client. Mouse-look itself has its own
 // stricter game_input_available() check.
 if(!m_game_window || GetForegroundWindow()!=m_game_window)return;
 CameraPose p{}; if(!m_bridge->read(p))return;
 float sens=m_sensitivity;
 float move_scale=down(VK_MENU)?0.20f:1.0f;
 float speed=.05f*sens*move_scale; if(down(VK_SHIFT))speed*=4.0f;
 float vertical=.035f*sens*move_scale; if(down(VK_SHIFT))vertical*=4.0f;
 Vec3 f{p.interest.x-p.position.x,p.interest.y-p.position.y,p.interest.z-p.position.z};
 float fl=len3(f); if(fl<.001f)fl=1; f.x/=fl;f.y/=fl;f.z/=fl;
 Vec3 r{f.z,0,-f.x}; float rl=std::sqrt(r.x*r.x+r.z*r.z); if(rl>.001f){r.x/=rl;r.z/=rl;}
 Vec3 mv{};
 if(down('W')){mv.x+=f.x;mv.y+=f.y;mv.z+=f.z;}
 if(down('S')){mv.x-=f.x;mv.y-=f.y;mv.z-=f.z;}
 if(down('D')){mv.x-=r.x;mv.z-=r.z;}
 if(down('A')){mv.x+=r.x;mv.z+=r.z;}
 if(down(VK_SPACE))mv.y+=vertical;
 if(down(VK_CONTROL))mv.y-=vertical;
 float ml=len3(mv);
 if(ml>.001f){mv.x=mv.x/ml*speed;mv.y=mv.y/ml*speed;mv.z=mv.z/ml*speed;p.position.x+=mv.x;p.position.y+=mv.y;p.position.z+=mv.z;p.interest.x+=mv.x;p.interest.y+=mv.y;p.interest.z+=mv.z;}
 update_mouse(p); update_roll(p); update_focal(p); update_fov(p); m_bridge->write(p);
}

void FreeCameraController::save_to_disk(int slot,const CameraPose&p){CreateDirectoryW((dll_dir()+L"\\save").c_str(),nullptr);std::wofstream f(save_path(slot));if(!f)return;f.precision(9);f<<p.position.x<<L' '<<p.position.y<<L' '<<p.position.z<<L'\n'<<p.interest.x<<L' '<<p.interest.y<<L' '<<p.interest.z<<L'\n'<<m_yaw<<L' '<<m_pitch<<L' '<<m_roll<<L'\n'<<p.perspective<<L' '<<p.near_clip<<L' '<<m_focal_distance<<L'\n';}
bool FreeCameraController::load_from_disk(int slot,CameraPose&p){std::wifstream f(save_path(slot));if(!f)return false;if(!(f>>p.position.x>>p.position.y>>p.position.z))return false;if(!(f>>p.interest.x>>p.interest.y>>p.interest.z))return false;if(!(f>>m_yaw>>m_pitch>>m_roll))return false;if(!(f>>p.perspective>>p.near_clip>>m_focal_distance))return false;return true;}
void FreeCameraController::save_slot(int slot){if(m_all_locked||slot<1||slot>9||!m_bridge||!m_bridge->enabled())return;CameraPose p{};if(m_bridge->read(p)){p.rot_y=m_roll;save_to_disk(slot,p);m_slots[slot-1]=p;m_slot_valid[slot-1]=true;char s[80];std::snprintf(s,sizeof(s),"Saved camera state %02d",slot);if(m_status)SetWindowTextA(m_status,s);}}
void FreeCameraController::load_slot(int slot){if(m_all_locked||slot<1||slot>9||!m_bridge||!m_bridge->enabled())return;CameraPose p{};if(!load_from_disk(slot,p)){if(!m_slot_valid[slot-1])return;p=m_slots[slot-1];}apply_orientation(p);m_bridge->set_roll(m_roll);m_bridge->write(p);refresh_ui();char s[80];std::snprintf(s,sizeof(s),"Loaded camera state %02d",slot);if(m_status)SetWindowTextA(m_status,s);}
void FreeCameraController::handle_state_hotkeys(){if(!m_enabled)return;for(int i=0;i<9;i++){int vk='1'+i;if(down(VK_MENU)){if(pressed_once(vk))save_slot(i+1);}else if(pressed_once(vk))load_slot(i+1);}}

void FreeCameraController::update(){
 if(!m_bridge||!m_bridge->valid())return;
 if(pressed_once(VK_F8))toggle();
 if(!m_enabled)return;
 static bool prev_f9=false;
 bool f9=down(VK_F9);
 bool f9_edge=f9 && !prev_f9;
 prev_f9=f9;
 if(pressed_once('H')){default_position();}
 if(down(VK_MENU) && pressed_once('U')){apply_ui();}
 if(f9_edge && down(VK_MENU)){
   m_all_locked=!m_all_locked; m_have_center=false;
   Log::write("FreeCameraController: ALT+F9 all values %s",m_all_locked?"LOCKED":"UNLOCKED"); return;
 }
 if(f9_edge && !down(VK_MENU)){
   m_mouse_locked=!m_mouse_locked; m_have_center=false;
   Log::write("FreeCameraController: F9 mouse look %s",m_mouse_locked?"OFF":"ON");
 }
 if(pressed_once(VK_F10)){
   if(m_ui&&IsWindowVisible(m_ui))close_ui(); else open_ui();
   m_have_center=false;
 }
 if(m_all_locked)return;
 if(pressed_once(VK_HOME))reset_pose();
 if(pressed_once('G'))reset_to_game_position();
 handle_state_hotkeys();
 if(m_bridge->enabled())update_freecam();
}
