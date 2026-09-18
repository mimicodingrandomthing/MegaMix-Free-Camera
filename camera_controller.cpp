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
#include <cctype>
#include <cstdlib>

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
static float clampf(float v,float lo,float hi){return std::max(lo,std::min(hi,v));}
static float wrap(float v){while(v>PI)v-=TWO_PI;while(v<-PI)v+=TWO_PI;return v;}
static float len3(Vec3 v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}

static void derive_angles(const CameraPose&p,float&yaw,float&pitch){Vec3 d{p.interest.x-p.position.x,p.interest.y-p.position.y,p.interest.z-p.position.z};float l=len3(d);if(l<.0001f)l=1; yaw=std::atan2(d.x,d.z);pitch=std::asin(clampf(d.y/l,-.9999f,.9999f));}
static void set_edit(HWND h,float v){char b[64];std::snprintf(b,sizeof(b),"%.6f",v);SetWindowTextA(h,b);}
static float read_edit(HWND h,float f){char b[64]{};GetWindowTextA(h,b,sizeof(b));char*e=nullptr;float v=std::strtof(b,&e);return(e==b)?f:v;}
static HWND make_control(HWND parent,LPCWSTR cls,LPCWSTR text,DWORD style,int x,int y,int w,int h,int id,HINSTANCE inst){ return CreateWindowExW(0,cls,text,style,x,y,w,h,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),inst,nullptr); }
static HWND label(HWND p,const char*t,int x,int y,int w=145){ wchar_t wt[512]{}; MultiByteToWideChar(CP_ACP,0,t,-1,wt,512); return make_control(p,L"STATIC",wt,WS_CHILD|WS_VISIBLE,x,y,w,20,0,GetModuleHandleW(nullptr)); }
static void set_slider(HWND h,int v){if(h)SendMessageA(h,TBM_SETPOS,TRUE,v);}
static int slider(HWND h){return h?(int)SendMessageA(h,TBM_GETPOS,0,0):0;}
static std::wstring dll_dir(){HMODULE hm=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&dll_dir),&hm);wchar_t path[MAX_PATH]{};GetModuleFileNameW(hm,path,MAX_PATH);std::wstring s(path);size_t p=s.find_last_of(L"\\/");return p==std::wstring::npos?L".":s.substr(0,p);}
static std::wstring save_path(int slot){return dll_dir()+L"\\save\\save0"+std::to_wstring(slot)+L".txt";}

static std::wstring keybind_path(){return dll_dir()+L"\\MegaMixFreeCam.ini";}

using KeyAction = FreeCameraController::KeyAction;
using KeyChord = FreeCameraController::KeyChord;
using KeyBinding = FreeCameraController::KeyBinding;

struct KeyDefaultEntry { const char* name; const char* value; };

static constexpr KeyDefaultEntry KEY_DEFAULTS[] = {
    {"ToggleFreeCam", "F8"},
    {"ToggleMouseLook", "F9"},
    {"ToggleAllLock", "ALT+F9"},
    {"ToggleUI", "F10"},
    {"ResetPose", "HOME"},
    {"GetGameCamera", "G"},
    {"DefaultCamera", "H"},
    {"ApplyUI", "ALT+U"},

    {"MoveForward", "W"},
    {"MoveBackward", "S"},
    {"MoveLeft", "A"},
    {"MoveRight", "D"},
    {"MoveUp", "SPACE"},
    {"MoveDown", "CTRL"},

    {"RollLeft", "Q"},
    {"RollRight", "E"},
    {"LookAtDecrease", "LBRACKET"},
    {"LookAtIncrease", "RBRACKET"},
    {"FovDecrease", "R"},
    {"FovIncrease", "F"},
    {"SensitivityDecrease", "MINUS,NUMPAD_MINUS"},
    {"SensitivityIncrease", "PLUS,NUMPAD_PLUS"},
    {"FastModifier", "SHIFT"},
    {"SlowModifier", "ALT"},

    {"LoadSlot1", "1"},
    {"LoadSlot2", "2"},
    {"LoadSlot3", "3"},
    {"LoadSlot4", "4"},
    {"LoadSlot5", "5"},
    {"LoadSlot6", "6"},
    {"LoadSlot7", "7"},
    {"LoadSlot8", "8"},
    {"LoadSlot9", "9"},

    {"SaveSlot1", "ALT+1"},
    {"SaveSlot2", "ALT+2"},
    {"SaveSlot3", "ALT+3"},
    {"SaveSlot4", "ALT+4"},
    {"SaveSlot5", "ALT+5"},
    {"SaveSlot6", "ALT+6"},
    {"SaveSlot7", "ALT+7"},
    {"SaveSlot8", "ALT+8"},
    {"SaveSlot9", "ALT+9"}
};

static_assert(
    sizeof(KEY_DEFAULTS)/sizeof(KEY_DEFAULTS[0]) == FreeCameraController::KEY_ACTION_COUNT,
    "KEY_DEFAULTS must match KeyAction"
);

static std::string trim_copy(std::string s){
    size_t a=0,b=s.size();
    while(a<b && std::isspace(static_cast<unsigned char>(s[a])))++a;
    while(b>a && std::isspace(static_cast<unsigned char>(s[b-1])))--b;
    return s.substr(a,b-a);
}

static std::string upper_copy(std::string s){
    for(char&c:s)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

static std::wstring widen_ascii(const char*s){
    std::wstring w;
    if(!s)return w;
    while(*s)w.push_back(static_cast<unsigned char>(*s++));
    return w;
}

static std::string narrow_ascii(const wchar_t*s){
    std::string a;
    if(!s)return a;
    while(*s)a.push_back(static_cast<char>(*s++));
    return a;
}

static int parse_key_token(std::string token){
    token=upper_copy(trim_copy(token));
    if(token.empty() || token=="NONE" || token=="DISABLED" || token=="OFF")return 0;
    if(token.size()==1){
        char c=token[0];
        if((c>='A'&&c<='Z')||(c>='0'&&c<='9'))return static_cast<int>(c);
        if(c=='-')return VK_OEM_MINUS;
        if(c=='+'||c=='=')return VK_OEM_PLUS;
        if(c=='[')return VK_OEM_4;
        if(c==']')return VK_OEM_6;
        if(c=='\\')return VK_OEM_5;
        if(c==';')return VK_OEM_1;
        if(c=='\'')return VK_OEM_7;
        if(c==',')return VK_OEM_COMMA;
        if(c=='.')return VK_OEM_PERIOD;
        if(c=='/')return VK_OEM_2;
        if(c=='`')return VK_OEM_3;
    }
    if(token[0]=='F' && token.size()<=3){
        int n=std::atoi(token.c_str()+1);
        if(n>=1&&n<=24)return VK_F1+(n-1);
    }
    if(token.rfind("NUMPAD",0)==0 && token.size()==7 && token[6]>='0'&&token[6]<='9')return VK_NUMPAD0+(token[6]-'0');

    if(token=="SPACE")return VK_SPACE;
    if(token=="CTRL"||token=="CONTROL")return VK_CONTROL;
    if(token=="LCTRL"||token=="LCONTROL")return VK_LCONTROL;
    if(token=="RCTRL"||token=="RCONTROL")return VK_RCONTROL;
    if(token=="ALT"||token=="MENU")return VK_MENU;
    if(token=="LALT"||token=="LMENU")return VK_LMENU;
    if(token=="RALT"||token=="RMENU")return VK_RMENU;
    if(token=="SHIFT")return VK_SHIFT;
    if(token=="LSHIFT")return VK_LSHIFT;
    if(token=="RSHIFT")return VK_RSHIFT;
    if(token=="HOME")return VK_HOME;
    if(token=="END")return VK_END;
    if(token=="INSERT"||token=="INS")return VK_INSERT;
    if(token=="DELETE"||token=="DEL")return VK_DELETE;
    if(token=="PAGEUP"||token=="PGUP")return VK_PRIOR;
    if(token=="PAGEDOWN"||token=="PGDN")return VK_NEXT;
    if(token=="UP")return VK_UP;
    if(token=="DOWN")return VK_DOWN;
    if(token=="LEFT")return VK_LEFT;
    if(token=="RIGHT")return VK_RIGHT;
    if(token=="TAB")return VK_TAB;
    if(token=="ENTER"||token=="RETURN")return VK_RETURN;
    if(token=="ESC"||token=="ESCAPE")return VK_ESCAPE;
    if(token=="BACKSPACE"||token=="BACK")return VK_BACK;
    if(token=="CAPSLOCK"||token=="CAPS")return VK_CAPITAL;

    if(token=="MINUS"||token=="OEM_MINUS")return VK_OEM_MINUS;
    if(token=="PLUS"||token=="OEM_PLUS"||token=="EQUALS")return VK_OEM_PLUS;
    if(token=="LBRACKET"||token=="LEFTBRACKET")return VK_OEM_4;
    if(token=="RBRACKET"||token=="RIGHTBRACKET")return VK_OEM_6;
    if(token=="BACKSLASH")return VK_OEM_5;
    if(token=="SEMICOLON")return VK_OEM_1;
    if(token=="APOSTROPHE"||token=="QUOTE")return VK_OEM_7;
    if(token=="COMMA")return VK_OEM_COMMA;
    if(token=="PERIOD"||token=="DOT")return VK_OEM_PERIOD;
    if(token=="SLASH")return VK_OEM_2;
    if(token=="GRAVE"||token=="BACKTICK")return VK_OEM_3;

    if(token=="NUMPAD_PLUS"||token=="NUMPLUS"||token=="ADD")return VK_ADD;
    if(token=="NUMPAD_MINUS"||token=="NUMMINUS"||token=="SUBTRACT")return VK_SUBTRACT;
    if(token=="NUMPAD_MULTIPLY"||token=="MULTIPLY")return VK_MULTIPLY;
    if(token=="NUMPAD_DIVIDE"||token=="DIVIDE")return VK_DIVIDE;
    if(token=="NUMPAD_DECIMAL"||token=="DECIMAL")return VK_DECIMAL;

    if(token=="MOUSE1"||token=="LMB")return VK_LBUTTON;
    if(token=="MOUSE2"||token=="RMB")return VK_RBUTTON;
    if(token=="MOUSE3"||token=="MMB")return VK_MBUTTON;
    if(token=="MOUSE4"||token=="XBUTTON1")return VK_XBUTTON1;
    if(token=="MOUSE5"||token=="XBUTTON2")return VK_XBUTTON2;
    return 0;
}

static bool modifier_from_token(const std::string&token,KeyChord&chord){
    std::string t=upper_copy(trim_copy(token));
    if(t=="CTRL"||t=="CONTROL"||t=="LCTRL"||t=="LCONTROL"||t=="RCTRL"||t=="RCONTROL"){chord.ctrl=true;return true;}
    if(t=="ALT"||t=="MENU"||t=="LALT"||t=="LMENU"||t=="RALT"||t=="RMENU"){chord.alt=true;return true;}
    if(t=="SHIFT"||t=="LSHIFT"||t=="RSHIFT"){chord.shift=true;return true;}
    return false;
}

static bool parse_chord(std::string raw,KeyChord&out){
    raw=trim_copy(raw);
    if(raw.empty())return false;
    std::string upper=upper_copy(raw);
    if(upper=="NONE"||upper=="DISABLED"||upper=="OFF"){out={};return true;}

    // Single-character punctuation such as '+' is a key, not a chord separator.
    if(raw.size()==1){
        int vk=parse_key_token(raw);
        if(!vk)return false;
        out={};out.vk=vk;return true;
    }

    std::string parts[4];
    int count=0;
    size_t start=0;
    for(size_t i=0;i<=raw.size();++i){
        if(i==raw.size()||raw[i]=='+'){
            if(count>=4)return false;
            parts[count++]=trim_copy(raw.substr(start,i-start));
            start=i+1;
        }
    }
    if(count<=0)return false;
    if(count==1){
        int vk=parse_key_token(parts[0]);
        if(!vk)return false;
        out={};out.vk=vk;return true;
    }
    out={};
    for(int i=0;i<count-1;i++)if(!modifier_from_token(parts[i],out))return false;
    out.vk=parse_key_token(parts[count-1]);
    return out.vk!=0;
}

static bool parse_binding(std::string raw,KeyBinding&out){
    out={};
    raw=trim_copy(raw);
    std::string upper=upper_copy(raw);
    if(upper=="NONE"||upper=="DISABLED"||upper=="OFF")return true;
    size_t start=0;
    bool any=false;
    while(start<=raw.size() && out.count<4){
        size_t comma=raw.find(',',start);
        std::string part=trim_copy(raw.substr(start,comma==std::string::npos?std::string::npos:comma-start));
        if(!part.empty()){
            KeyChord chord{};
            if(parse_chord(part,chord) && chord.vk){
                out.chords[out.count++]=chord;
                any=true;
            }
        }
        if(comma==std::string::npos)break;
        start=comma+1;
    }
    return any;
}

static bool chord_is_down(const KeyChord&c){
    if(!c.vk)return false;
    if(c.ctrl && !down(VK_CONTROL))return false;
    if(c.alt && !down(VK_MENU))return false;
    if(c.shift && !down(VK_SHIFT))return false;
    return down(c.vk);
}

static bool binding_is_down(const KeyBinding&binding){
    for(int i=0;i<binding.count;i++)if(chord_is_down(binding.chords[i]))return true;
    return false;
}

static std::string key_name(int vk){
    if(vk>='A'&&vk<='Z')return std::string(1,static_cast<char>(vk));
    if(vk>='0'&&vk<='9')return std::string(1,static_cast<char>(vk));
    if(vk>=VK_F1&&vk<=VK_F24)return "F"+std::to_string(vk-VK_F1+1);
    if(vk>=VK_NUMPAD0&&vk<=VK_NUMPAD9)return "Num"+std::to_string(vk-VK_NUMPAD0);
    switch(vk){
        case VK_SPACE:return "Space";
        case VK_CONTROL:return "Ctrl";
        case VK_LCONTROL:return "LCtrl";
        case VK_RCONTROL:return "RCtrl";
        case VK_MENU:return "Alt";
        case VK_LMENU:return "LAlt";
        case VK_RMENU:return "RAlt";
        case VK_SHIFT:return "Shift";
        case VK_LSHIFT:return "LShift";
        case VK_RSHIFT:return "RShift";
        case VK_HOME:return "Home";
        case VK_END:return "End";
        case VK_INSERT:return "Insert";
        case VK_DELETE:return "Delete";
        case VK_PRIOR:return "PgUp";
        case VK_NEXT:return "PgDn";
        case VK_UP:return "Up";
        case VK_DOWN:return "Down";
        case VK_LEFT:return "Left";
        case VK_RIGHT:return "Right";
        case VK_TAB:return "Tab";
        case VK_RETURN:return "Enter";
        case VK_ESCAPE:return "Esc";
        case VK_BACK:return "Backspace";
        case VK_CAPITAL:return "CapsLock";
        case VK_OEM_MINUS:return "-";
        case VK_OEM_PLUS:return "+";
        case VK_OEM_4:return "[";
        case VK_OEM_6:return "]";
        case VK_OEM_5:return "\\";
        case VK_OEM_1:return ";";
        case VK_OEM_7:return "'";
        case VK_OEM_COMMA:return ",";
        case VK_OEM_PERIOD:return ".";
        case VK_OEM_2:return "/";
        case VK_OEM_3:return "`";
        case VK_ADD:return "Num+";
        case VK_SUBTRACT:return "Num-";
        case VK_MULTIPLY:return "Num*";
        case VK_DIVIDE:return "Num/";
        case VK_DECIMAL:return "Num.";
        case VK_LBUTTON:return "Mouse1";
        case VK_RBUTTON:return "Mouse2";
        case VK_MBUTTON:return "Mouse3";
        case VK_XBUTTON1:return "Mouse4";
        case VK_XBUTTON2:return "Mouse5";
        default:return "VK_"+std::to_string(vk);
    }
}

static std::string chord_text(const KeyChord&c){
    if(!c.vk)return "None";
    std::string s;
    if(c.ctrl)s+="Ctrl+";
    if(c.alt)s+="Alt+";
    if(c.shift)s+="Shift+";
    s+=key_name(c.vk);
    return s;
}

static void set_text(HWND h,const std::string&s){if(h)SetWindowTextA(h,s.c_str());}

static void write_default_ini_if_missing(){
    std::wstring path=keybind_path();
    if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES)return;
    FILE*f=nullptr;
    if(_wfopen_s(&f,path.c_str(),L"wt")!=0||!f)return;
    std::fputs(
        "; MegaMix Free Camera keybinds\n"
        "; Format: KEY, MODIFIER+KEY, or multiple alternatives separated by commas.\n"
        "; Examples: F8, ALT+F9, MOUSE4, PLUS, NUMPAD_PLUS\n"
        "; Use NONE to disable a binding. Close/reopen the UI after editing this file in-game.\n\n"
        "[Keybinds]\n",f);
    for(const auto&e:KEY_DEFAULTS)std::fprintf(f,"%s=%s\n",e.name,e.value);
    std::fclose(f);
}

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
            if(self->m_hotkey_summary) SetWindowPos(self->m_hotkey_summary,nullptr,margin,h-70,std::max(300,w-30),20,SWP_NOZORDER|SWP_NOACTIVATE);
            if(self->m_status) SetWindowPos(self->m_status,nullptr,margin,h-45,std::max(300,w-30),25,SWP_NOZORDER|SWP_NOACTIVATE);
            return 0;
        }
        if(msg==WM_CLOSE){ShowWindow(hwnd,SW_HIDE);return 0;}
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}

void FreeCameraController::load_keybinds(){
    write_default_ini_if_missing();
    const std::wstring path=keybind_path();

    for(int i=0;i<KEY_ACTION_COUNT;i++){
        const auto&e=KEY_DEFAULTS[i];
        std::wstring key=widen_ascii(e.name);
        std::wstring def=widen_ascii(e.value);
        wchar_t buf[512]{};
        GetPrivateProfileStringW(L"Keybinds",key.c_str(),def.c_str(),buf,static_cast<DWORD>(sizeof(buf)/sizeof(buf[0])),path.c_str());

        KeyBinding binding{};
        std::string raw=narrow_ascii(buf);
        if(!parse_binding(raw,binding)){
            parse_binding(e.value,binding);
            Log::write("FreeCameraController: invalid keybind %s=%s, using default %s",e.name,raw.c_str(),e.value);
        }
        m_keybinds[i]=binding;

        // Seed the edge detector with the current physical state. This avoids
        // a key that opened the UI from immediately firing again when the INI
        // is reloaded while that key is still held.
        m_binding_down[i]=binding_is_down(binding);
        m_binding_pressed[i]=false;
    }

    update_ui_key_labels();
    Log::write("FreeCameraController: keybinds loaded from MegaMixFreeCam.ini");
}

void FreeCameraController::update_binding_states(){
    for(int i=0;i<KEY_ACTION_COUNT;i++){
        bool now=binding_is_down(m_keybinds[i]);
        m_binding_pressed[i]=now&&!m_binding_down[i];
        m_binding_down[i]=now;
    }
}

bool FreeCameraController::action_down(KeyAction action) const{
    int i=static_cast<int>(action);
    return i>=0&&i<KEY_ACTION_COUNT?m_binding_down[i]:false;
}

bool FreeCameraController::action_pressed(KeyAction action) const{
    int i=static_cast<int>(action);
    return i>=0&&i<KEY_ACTION_COUNT?m_binding_pressed[i]:false;
}

std::string FreeCameraController::binding_text(KeyAction action) const{
    int i=static_cast<int>(action);
    if(i<0||i>=KEY_ACTION_COUNT)return "None";
    const KeyBinding&binding=m_keybinds[i];
    if(binding.count<=0)return "None";
    std::string s;
    for(int n=0;n<binding.count;n++){
        if(n)s+=" / ";
        s+=chord_text(binding.chords[n]);
    }
    return s;
}

void FreeCameraController::update_ui_key_labels(){
    if(!m_ui)return;

    auto bt=[this](KeyAction a){return binding_text(a);};

    set_text(m_value_labels[0],"Position X   ["+bt(KeyAction::MoveLeft)+" / "+bt(KeyAction::MoveRight)+"]");
    set_text(m_value_labels[1],"Position Y   ["+bt(KeyAction::MoveUp)+" / "+bt(KeyAction::MoveDown)+"]");
    set_text(m_value_labels[2],"Position Z   ["+bt(KeyAction::MoveForward)+" / "+bt(KeyAction::MoveBackward)+"]");
    set_text(m_value_labels[3],"Interest X");
    set_text(m_value_labels[4],"Interest Y");
    set_text(m_value_labels[5],"Interest Z");
    set_text(m_value_labels[6],"Yaw (degrees)   [Mouse X]");
    set_text(m_value_labels[7],"Pitch (degrees) [Mouse Y]");
    set_text(m_value_labels[8],"Roll (degrees)  ["+bt(KeyAction::RollLeft)+" / "+bt(KeyAction::RollRight)+"]");
    set_text(m_value_labels[9],"FOV (degrees)   ["+bt(KeyAction::FovDecrease)+" / "+bt(KeyAction::FovIncrease)+"]");
    set_text(m_value_labels[10],"Near Clip");
    set_text(m_value_labels[11],"Focal distance   ["+bt(KeyAction::LookAtDecrease)+" / "+bt(KeyAction::LookAtIncrease)+"]");

    set_text(m_sensitivity_title,
        "Master sensitivity  ["+bt(KeyAction::SensitivityDecrease)+" / "+bt(KeyAction::SensitivityIncrease)+
        "]  |  Slow: "+bt(KeyAction::SlowModifier)+"  |  Fast: "+bt(KeyAction::FastModifier));

    set_text(m_apply_button,"Apply ("+bt(KeyAction::ApplyUI)+")");
    set_text(m_reset_game_button,"Game Camera ("+bt(KeyAction::GetGameCamera)+")");
    set_text(m_default_button,"Default ("+bt(KeyAction::DefaultCamera)+")");

    set_text(m_load_label,"LOAD SLOT");
    set_text(m_save_label,"SAVE SLOT");
    for(int i=0;i<9;i++){
        KeyAction load=static_cast<KeyAction>(static_cast<int>(KeyAction::LoadSlot1)+i);
        KeyAction save=static_cast<KeyAction>(static_cast<int>(KeyAction::SaveSlot1)+i);
        set_text(m_load_buttons[i],bt(load));
        set_text(m_save_buttons[i],bt(save));
    }

    std::string summary=
        "Freecam: "+bt(KeyAction::ToggleFreeCam)+
        "   |   UI: "+bt(KeyAction::ToggleUI)+
        "   |   Mouse: "+bt(KeyAction::ToggleMouseLook)+
        "   |   Lock: "+bt(KeyAction::ToggleAllLock)+
        "   |   Reset: "+bt(KeyAction::ResetPose);
    set_text(m_hotkey_summary,summary);
}

void FreeCameraController::initialize(MegaMixCameraBridge*bridge){
 m_bridge=bridge;
 load_keybinds();
 Log::write("FreeCameraController: configurable INI keybinds + FOV/Near sync initialized");
}
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
   // Deliberately do NOT open the UI when freecam is toggled. The UI has its own configurable toggle.
   Log::write("FreeCameraController: FREECAM ON (UI remains hidden)");
 } else {m_have_center=false;close_ui();Log::write("FreeCameraController: FREECAM OFF");}
}

void FreeCameraController::open_ui(){
 // Re-read the INI each time the UI is opened. This lets users edit the
 // keybind file while the game is running, then close/reopen the UI to apply it.
 load_keybinds();
 if(m_ui){ShowWindow(m_ui,SW_SHOW);SetTimer(m_ui,UI_TIMER,100,nullptr);update_ui_key_labels();refresh_ui();return;}
 INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_BAR_CLASSES};InitCommonControlsEx(&ic);
 WNDCLASSW wc{};wc.lpfnWndProc=UiProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"MegaMixFreeCamControlV27";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&wc);
 m_ui=CreateWindowExW(WS_EX_TOOLWINDOW,L"MegaMixFreeCamControlV27",L"MegaMix+ Free Camera Controller",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_VISIBLE,80,60,900,760,m_game_window,nullptr,wc.hInstance,this);

 const char* names[]={
     "Position X","Position Y","Position Z",
     "Interest X","Interest Y","Interest Z",
     "Yaw (degrees)","Pitch (degrees)","Roll (degrees)",
     "FOV (degrees)","Near Clip"
 };
 const int row_h=58, left=15, right=455, edit_w=190, slider_w=190;

 for(int i=0;i<6;i++){
     int col=i/3, row=i%3;
     int x=(col==0)?left:right, y=15+row*row_h;
     m_value_labels[i]=label(m_ui,names[i],x,y,edit_w+slider_w);
     m_edits[i]=make_control(m_ui,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,x,y+22,edit_w,24,2000+i,wc.hInstance);
 }

 for(int i=6;i<11;i++){
     int j=i-6, col=j/3, row=j%3;
     int x=(col==0)?left:right, y=205+row*58;
     m_value_labels[i]=label(m_ui,names[i],x,y,edit_w+slider_w);
     m_edits[i]=make_control(m_ui,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,x,y+22,edit_w,24,2000+i,wc.hInstance);
     int sx=x+205;
     if(i==10){
         m_sliders[4]=make_control(m_ui,TRACKBAR_CLASSW,L"",WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS,sx,y+20,slider_w,28,ID_SLIDER_BASE+4,wc.hInstance);
     } else {
         int si=i-6;
         m_sliders[si]=make_control(m_ui,TRACKBAR_CLASSW,L"",WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS,sx,y+20,slider_w,28,ID_SLIDER_BASE+si,wc.hInstance);
     }
 }

 int fy=379;
 m_value_labels[11]=label(m_ui,"Focal distance",right,fy,edit_w+slider_w);
 m_focal_edit=make_control(m_ui,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,right,fy+22,edit_w,24,2011,wc.hInstance);
 m_sliders[5]=make_control(m_ui,TRACKBAR_CLASSW,L"",WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS,right+205,fy+20,slider_w,28,ID_SLIDER_BASE+5,wc.hInstance);

 int sy=455;
 m_sensitivity_title=label(m_ui,"Master sensitivity",left,sy,650);
 m_sensitivity_slider=make_control(m_ui,TRACKBAR_CLASSW,L"",WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS,left,sy+22,520,28,ID_SENS,wc.hInstance);
 m_sensitivity_label=make_control(m_ui,L"STATIC",L"100%",WS_CHILD|WS_VISIBLE,left+530,sy+22,80,24,0,wc.hInstance);

 label(m_ui,"EDIT ANY VALUE BELOW, THEN PRESS Apply. Position/Interest are world coordinates; Yaw/Pitch/Focal control the viewing direction when Interest is unchanged.",left,500,850);
 m_apply_button=make_control(m_ui,L"BUTTON",L"Apply All Values",WS_CHILD|WS_VISIBLE,left,530,135,30,ID_APPLY,wc.hInstance);
 make_control(m_ui,L"BUTTON",L"Refresh / Capture",WS_CHILD|WS_VISIBLE,160,530,135,30,ID_CAPTURE,wc.hInstance);
 m_reset_game_button=make_control(m_ui,L"BUTTON",L"Get Game Camera",WS_CHILD|WS_VISIBLE,315,530,150,30,ID_RESET_GAME,wc.hInstance);
 m_default_button=make_control(m_ui,L"BUTTON",L"Default Camera",WS_CHILD|WS_VISIBLE,475,530,140,30,ID_DEFAULT,wc.hInstance);
 make_control(m_ui,L"BUTTON",L"Sync FOV + Near Clip",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,625,530,190,30,ID_SYNC,wc.hInstance);

 m_load_label=label(m_ui,"LOAD SLOT",left,580,150);
 m_save_label=label(m_ui,"SAVE SLOT",right,580,150);
 for(int i=0;i<9;i++){
     int col=i%5,row=i/5;
     int x=left+col*70,yy=605+row*32;
     m_load_buttons[i]=make_control(m_ui,L"BUTTON",L"",WS_CHILD|WS_VISIBLE,x,yy,60,27,ID_LOAD_BASE+i,wc.hInstance);
     m_save_buttons[i]=make_control(m_ui,L"BUTTON",L"",WS_CHILD|WS_VISIBLE,right+col*70,yy,60,27,ID_SAVE_BASE+i,wc.hInstance);
 }

 m_hotkey_summary=label(m_ui,"",left,655,850);
 m_status=make_control(m_ui,L"STATIC",L"",WS_CHILD|WS_VISIBLE,left,680,850,35,ID_STATUS,wc.hInstance);

 SetTimer(m_ui,UI_TIMER,100,nullptr);
 set_slider(m_sensitivity_slider,100);
 for(int i=0;i<6;i++)set_slider(m_sliders[i],50);

 SendMessageA(m_sliders[0],TBM_SETRANGE,TRUE,MAKELONG(0,100));
 SendMessageA(m_sliders[1],TBM_SETRANGE,TRUE,MAKELONG(0,100));
 SendMessageA(m_sliders[2],TBM_SETRANGE,TRUE,MAKELONG(0,100));
 SendMessageA(m_sliders[3],TBM_SETRANGE,TRUE,MAKELONG(0,100));
 SendMessageA(m_sliders[4],TBM_SETRANGE,TRUE,MAKELONG(0,100));
 SendMessageA(m_sliders[5],TBM_SETRANGE,TRUE,MAKELONG(0,100));

 update_ui_key_labels();
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
 bool left=action_down(KeyAction::RollLeft), right=action_down(KeyAction::RollRight);
 const bool fast=action_down(KeyAction::FastModifier);
 const float step=(fast?90.0f:1.0f)*DEG*m_sensitivity;
 if(fast){
   if(action_pressed(KeyAction::RollLeft))m_roll=wrap(m_roll+90.0f*DEG);
   else if(action_pressed(KeyAction::RollRight))m_roll=wrap(m_roll-90.0f*DEG);
 } else {
   if(left)m_roll+=step;
   if(right)m_roll-=step;
   m_roll=wrap(m_roll);
 }
 // Roll is stored in freecam state and applied to the existing engine up-vector
 // state by the bridge. Reapply it every frame so a native A3DA up-vector update
 // cannot erase the current freecam roll. Do not feed it through the rotation getter.
 m_bridge->set_roll(m_roll);
 pose.rot_y=m_roll;
}

void FreeCameraController::update_focal(CameraPose&pose){
 float fs=(action_down(KeyAction::FastModifier)?0.20f:0.05f)*m_sensitivity;
 if(action_down(KeyAction::LookAtDecrease))m_focal_distance=std::max(.05f,m_focal_distance-fs);
 if(action_down(KeyAction::LookAtIncrease))m_focal_distance+=fs;
 apply_orientation(pose);
}

void FreeCameraController::update_fov(CameraPose&pose){
 float fs=(action_down(KeyAction::FastModifier)?2.0f:.25f)*m_sensitivity;
 if(action_down(KeyAction::FovDecrease))pose.perspective=clampf(pose.perspective-fs,1,179);
 if(action_down(KeyAction::FovIncrease))pose.perspective=clampf(pose.perspective+fs,1,179);
}

void FreeCameraController::update_sensitivity_hotkeys(){
 if(!m_game_window || GetForegroundWindow()!=m_game_window)return;
 bool changed=false;
 if(action_pressed(KeyAction::SensitivityDecrease)){m_sensitivity=clampf(m_sensitivity-0.05f,0.10f,3.0f);changed=true;}
 if(action_pressed(KeyAction::SensitivityIncrease)){m_sensitivity=clampf(m_sensitivity+0.05f,0.10f,3.0f);changed=true;}
 if(changed){
   set_slider(m_sensitivity_slider,(int)clampf(m_sensitivity*100.0f,10.0f,300.0f));
   char b[64];std::snprintf(b,sizeof(b),"Sensitivity: %.0f%%",m_sensitivity*100.0f);if(m_sensitivity_label)SetWindowTextA(m_sensitivity_label,b);
 }
}

void FreeCameraController::update_freecam(){
 if(!m_bridge||m_all_locked)return;
 // The game must own the foreground window, but FOV/focal controls should not
 // depend on the mouse being inside the client. Mouse-look itself has its own
 // stricter game_input_available() check.
 if(!m_game_window || GetForegroundWindow()!=m_game_window)return;
 CameraPose p{}; if(!m_bridge->read(p))return;
 float sens=m_sensitivity;
 float move_scale=action_down(KeyAction::SlowModifier)?0.20f:1.0f;
 float speed=.05f*sens*move_scale; if(action_down(KeyAction::FastModifier))speed*=4.0f;
 float vertical=.035f*sens*move_scale; if(action_down(KeyAction::FastModifier))vertical*=4.0f;
 Vec3 f{p.interest.x-p.position.x,p.interest.y-p.position.y,p.interest.z-p.position.z};
 float fl=len3(f); if(fl<.001f)fl=1; f.x/=fl;f.y/=fl;f.z/=fl;
 Vec3 r{f.z,0,-f.x}; float rl=std::sqrt(r.x*r.x+r.z*r.z); if(rl>.001f){r.x/=rl;r.z/=rl;}
 Vec3 mv{};
 if(action_down(KeyAction::MoveForward)){mv.x+=f.x;mv.y+=f.y;mv.z+=f.z;}
 if(action_down(KeyAction::MoveBackward)){mv.x-=f.x;mv.y-=f.y;mv.z-=f.z;}
 if(action_down(KeyAction::MoveRight)){mv.x-=r.x;mv.z-=r.z;}
 if(action_down(KeyAction::MoveLeft)){mv.x+=r.x;mv.z+=r.z;}
 if(action_down(KeyAction::MoveUp))mv.y+=vertical;
 if(action_down(KeyAction::MoveDown))mv.y-=vertical;
 float ml=len3(mv);
 if(ml>.001f){mv.x=mv.x/ml*speed;mv.y=mv.y/ml*speed;mv.z=mv.z/ml*speed;p.position.x+=mv.x;p.position.y+=mv.y;p.position.z+=mv.z;p.interest.x+=mv.x;p.interest.y+=mv.y;p.interest.z+=mv.z;}
 update_mouse(p); update_roll(p); update_focal(p); update_fov(p); m_bridge->write(p);
}

void FreeCameraController::save_to_disk(int slot,const CameraPose&p){CreateDirectoryW((dll_dir()+L"\\save").c_str(),nullptr);std::wofstream f(save_path(slot));if(!f)return;f.precision(9);f<<p.position.x<<L' '<<p.position.y<<L' '<<p.position.z<<L'\n'<<p.interest.x<<L' '<<p.interest.y<<L' '<<p.interest.z<<L'\n'<<m_yaw<<L' '<<m_pitch<<L' '<<m_roll<<L'\n'<<p.perspective<<L' '<<p.near_clip<<L' '<<m_focal_distance<<L'\n';}
bool FreeCameraController::load_from_disk(int slot,CameraPose&p){std::wifstream f(save_path(slot));if(!f)return false;if(!(f>>p.position.x>>p.position.y>>p.position.z))return false;if(!(f>>p.interest.x>>p.interest.y>>p.interest.z))return false;if(!(f>>m_yaw>>m_pitch>>m_roll))return false;if(!(f>>p.perspective>>p.near_clip>>m_focal_distance))return false;return true;}
void FreeCameraController::save_slot(int slot){if(m_all_locked||slot<1||slot>9||!m_bridge||!m_bridge->enabled())return;CameraPose p{};if(m_bridge->read(p)){p.rot_y=m_roll;save_to_disk(slot,p);m_slots[slot-1]=p;m_slot_valid[slot-1]=true;char s[80];std::snprintf(s,sizeof(s),"Saved camera state %02d",slot);if(m_status)SetWindowTextA(m_status,s);}}
void FreeCameraController::load_slot(int slot){if(m_all_locked||slot<1||slot>9||!m_bridge||!m_bridge->enabled())return;CameraPose p{};if(!load_from_disk(slot,p)){if(!m_slot_valid[slot-1])return;p=m_slots[slot-1];}apply_orientation(p);m_bridge->set_roll(m_roll);m_bridge->write(p);refresh_ui();char s[80];std::snprintf(s,sizeof(s),"Loaded camera state %02d",slot);if(m_status)SetWindowTextA(m_status,s);}
void FreeCameraController::handle_state_hotkeys(){
 if(!m_enabled)return;
 for(int i=0;i<9;i++){
   KeyAction load=static_cast<KeyAction>(static_cast<int>(KeyAction::LoadSlot1)+i);
   KeyAction save=static_cast<KeyAction>(static_cast<int>(KeyAction::SaveSlot1)+i);
   // Save wins if a custom save chord overlaps a load chord (the defaults are Alt+1..9 vs 1..9).
   if(action_pressed(save))save_slot(i+1);
   else if(action_pressed(load))load_slot(i+1);
 }
}

void FreeCameraController::update(){
 if(!m_bridge||!m_bridge->valid())return;

 update_binding_states();

 if(action_pressed(KeyAction::ToggleFreeCam))toggle();
 if(!m_enabled)return;

 if(action_pressed(KeyAction::DefaultCamera))default_position();
 if(action_pressed(KeyAction::ApplyUI))apply_ui();

 // Process the more-specific lock chord before mouse-look. If the bindings
 // overlap (default Alt+F9 vs F9), the lock action wins for that key press.
 if(action_pressed(KeyAction::ToggleAllLock)){
   m_all_locked=!m_all_locked; m_have_center=false;
   Log::write("FreeCameraController: all values %s",m_all_locked?"LOCKED":"UNLOCKED");
 }
 else if(action_pressed(KeyAction::ToggleMouseLook)){
   m_mouse_locked=!m_mouse_locked; m_have_center=false;
   Log::write("FreeCameraController: mouse look %s",m_mouse_locked?"OFF":"ON");
 }

 if(action_pressed(KeyAction::ToggleUI)){
   if(m_ui&&IsWindowVisible(m_ui))close_ui(); else open_ui();
   m_have_center=false;
 }

 if(m_all_locked)return;

 update_sensitivity_hotkeys();
 if(action_pressed(KeyAction::ResetPose))reset_pose();
 if(action_pressed(KeyAction::GetGameCamera))reset_to_game_position();
 handle_state_hotkeys();
 if(m_bridge->enabled())update_freecam();
}
