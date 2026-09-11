#include <SekiroVisionAI/AutoDodge.h>
#include "StatusOverlay.h"
#include <shellapi.h>
#include <windowsx.h>
#include <commdlg.h>
#include <iomanip>
#include <sstream>

namespace {
using namespace sekiro;
enum ControlId { Start=301,Stop,Toggle,Advanced,Overlay,Mode,Provider,Model,Direction,ManualRoi,ResetRoi,Record,OpenLog };
std::wstring wide(const std::string& value){return {value.begin(),value.end()};}
std::wstring number(double value,int precision=1){if(!std::isfinite(value))return L"unavailable";std::wostringstream out;out<<std::fixed<<std::setprecision(precision)<<value;return out.str();}
class Application {
public:
    AutoDodge dodge;
    CaptureEngine capture; // Stops before the callback owner is destroyed.
    StatusOverlay overlay;
    HWND window{},heading{},start{},stop{},toggle{},advanced{},overlay_control{},status{},events{};
    HWND mode{},provider{},model{},direction{},manual_roi{},reset_roi{},record{},open_log{};
    HFONT font{};
    UINT dpi{96};
    WindowTarget game{};
    MvpSnapshot latest;
    RECT preview{};
    bool debug{},closing{},dragging{},smoke{},overlay_on{true};
    POINT drag_start{},drag_end{};
    double launched{qpc_ms()},last_discovery{};
    int exit_code{};
    ~Application(){if(font)DeleteObject(font);}
    int px(int n)const{return MulDiv(n,static_cast<int>(dpi),96);}
    HWND control(const wchar_t* klass,const wchar_t* title,DWORD style,int id){
        auto h=CreateWindowExW(0,klass,title,WS_VISIBLE|WS_CHILD|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!h)throw std::runtime_error("Could not create application control");return h;
    }
    void create(HWND hwnd){
        window=hwnd;dpi=GetDpiForWindow(hwnd);
        heading=control(L"STATIC",L"SekiroVisionAI\r\nOpen Sekiro, then Start. F8: Auto Dodge   |   F9: emergency stop",0,0);
        start=control(L"BUTTON",L"Start",WS_TABSTOP,Start);stop=control(L"BUTTON",L"Stop",WS_TABSTOP,Stop);
        toggle=control(L"BUTTON",L"Auto Dodge: OFF",WS_TABSTOP,Toggle);
        overlay_control=control(L"BUTTON",L"Overlay",BS_AUTOCHECKBOX|WS_TABSTOP,Overlay);
        advanced=control(L"BUTTON",L"Advanced / Debug",BS_AUTOCHECKBOX|WS_TABSTOP,Advanced);
        status=control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|WS_VSCROLL,0);
        events=control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,0);
        mode=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,Mode);
        provider=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,Provider);
        model=control(L"BUTTON",L"Debug model...",WS_TABSTOP,Model);
        direction=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,Direction);
        manual_roi=control(L"BUTTON",L"Manual ROI",BS_AUTOCHECKBOX|WS_TABSTOP,ManualRoi);
        reset_roi=control(L"BUTTON",L"Automatic ROI",WS_TABSTOP,ResetRoi);
        record=control(L"BUTTON",L"Collect event samples",BS_AUTOCHECKBOX|WS_TABSTOP,Record);
        open_log=control(L"BUTTON",L"Open log",WS_TABSTOP,OpenLog);
        for(auto label:{L"Managed temporal model",L"CV heuristic (Debug)"})SendMessageW(mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        for(auto label:{L"Automatic provider",L"DirectML",L"CPU",L"CUDA (compatible build)"})SendMessageW(provider,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        for(auto label:{L"Automatic direction",L"Shift / physical movement",L"Left: A + Shift",L"Right: D + Shift",L"Back: S + Shift",L"Forward: W + Shift"})SendMessageW(direction,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        const auto config=dodge.config();
        SendMessageW(mode,CB_SETCURSEL,config.detector_mode?0:1,0);
        SendMessageW(provider,CB_SETCURSEL,config.provider=="DirectML"?1:config.provider=="CPU"?2:config.provider=="CUDA"?3:0,0);
        SendMessageW(direction,CB_SETCURSEL,config.direction+1,0);
        SendMessageW(manual_roi,BM_SETCHECK,config.automatic_roi?BST_UNCHECKED:BST_CHECKED,0);
        SendMessageW(overlay_control,BM_SETCHECK,BST_CHECKED,0);
        capture.set_frame_sink([this](const SmallFrame& frame){dodge.submit(frame);},[this]{dodge.discontinuity();});
        apply_font();layout();discover();update();SetTimer(window,1,100,nullptr);
    }
    void apply_font(){
        auto next=CreateFontW(-px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        for(auto h:{heading,start,stop,toggle,advanced,overlay_control,status,events,mode,provider,model,direction,manual_roi,reset_roi,record,open_log})SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if(font)DeleteObject(font);font=next;
    }
    std::shared_ptr<const ColorFrame> display_frame()const{return latest.preview.preview_color?latest.preview.preview_color:latest.preview.color;}
    void layout(){
        RECT r{};GetClientRect(window,&r);const int w=MulDiv(r.right,96,static_cast<int>(dpi)),h=MulDiv(r.bottom,96,static_cast<int>(dpi));
        auto move=[&](HWND item,int x,int y,int width,int height){MoveWindow(item,px(x),px(y),px(width),px(height),TRUE);};
        move(heading,20,15,w-40,50);move(start,20,77,100,34);move(stop,130,77,100,34);move(toggle,240,77,225,34);
        move(overlay_control,480,79,105,28);move(advanced,w-195,79,175,28);
        const int top=debug?205:140,left_width=(w-60)*52/100;
        for(auto item:{mode,provider,model,direction,manual_roi,reset_roi,record,open_log})ShowWindow(item,debug?SW_SHOW:SW_HIDE);
        move(mode,20,123,205,160);move(provider,235,123,185,160);move(model,430,122,155,30);move(direction,595,123,w-615,190);
        move(manual_roi,20,161,120,28);move(reset_roi,150,159,135,30);move(record,300,161,195,28);move(open_log,510,159,100,30);
        const auto image=display_frame();
        const double aspect=image?static_cast<double>(image->height)/image->width:9.0/16;
        const int image_height=std::min(340,static_cast<int>(left_width*aspect));
        const int image_width=std::min(left_width,static_cast<int>(image_height/aspect));
        preview={px(20),px(top+24),px(20+image_width),px(top+24+image_height)};
        move(status,left_width+40,top,w-left_width-60,debug?350:330);
        const int log_top=std::max(top+365,top+image_height+78);
        move(events,20,log_top,w-40,std::max(65,h-log_top-20));InvalidateRect(window,nullptr,TRUE);
    }
    void discover(){
        if(capture.snapshot().active||closing)return;
        const auto found=find_sekiro_windows();
        if(found.empty()){game={};return;}
        game=found.front();
        for(const auto& target:found)if(target.hwnd==GetForegroundWindow()){game=target;break;}
    }
    void update(){
        const double now=qpc_ms();auto cap=capture.snapshot();const auto old_color=display_frame();latest=dodge.snapshot();const auto shown=display_frame();
        const bool cv_mode=dodge.config().detector_mode==0;
        if(!cap.active&&now-last_discovery>=1500){discover();last_discovery=now;}
        if(shown&&(!old_color||old_color->width!=shown->width||old_color->height!=shown->height))layout();
        if(smoke&&!closing&&now-launched>3000){
            exit_code=latest.input.hotkeys_ready&&latest.input.watchdog_ready?0:2;
            dodge.log()->emit("UI_SMOKE_RESULT",exit_code==0?"Native controls and watchdog initialized; no input sent":"Hotkey/watchdog unavailable");close();return;
        }
        if(closing&&!cap.active){DestroyWindow(window);return;}
        EnableWindow(start,!cap.active&&game.hwnd&&!closing);EnableWindow(stop,cap.active&&!closing);
        EnableWindow(toggle,cap.active&&!closing);
        SetWindowTextW(toggle,latest.input.enabled?L"Auto Dodge: ON":latest.input.arm_pending?L"Armed: return to Sekiro":L"Auto Dodge: OFF");
        SetWindowTextW(window,latest.input.enabled?L"SekiroVisionAI - Auto Dodge ON":L"SekiroVisionAI");
        SendMessageW(record,BM_SETCHECK,latest.recording.enabled?BST_CHECKED:BST_UNCHECKED,0);
        std::wostringstream text;
        text<<L"Game: "<<(game.hwnd?L"Sekiro detected":L"Waiting for Sekiro")
            <<L"\r\nCapture: "<<state_name(cap.state)<<L"   "<<number(cap.delivered_fps)<<L" FPS delivered"
            <<L"\r\nGPU: "<<cap.adapter
            <<L"\r\nDetector: "<<(cv_mode?L"CV heuristic (Debug)":latest.model.loaded?L"Temporal model":L"Temporal model unavailable")
            <<L"\r\nModel: "<<(cv_mode?L"not in use":latest.model.loaded?wide(latest.model.version):L"unavailable")
            <<L"\r\nProvider: "<<(cv_mode?L"not in use":wide(latest.model.provider))
            <<L"\r\nAuto Dodge: "<<(latest.input.enabled?L"ON":latest.input.arm_pending?L"pending game focus":L"OFF")
            <<L"\r\n"<<wide(latest.input.reason)
            <<L"\r\n\r\nTarget: "<<wide(latest.target_status);
        const auto& prediction=latest.prediction;
        if(cv_mode){
            text<<L"\r\nCV motion score / quality: "<<number(latest.motion.score,3)<<L" / "<<number(latest.motion.confidence,3)
                <<L"\r\nAttack probability / TTI: unavailable (CV heuristic)";
        }else if(prediction.valid&&prediction.trained&&prediction.attack_supported&&fresh_at(prediction.source_ms,now,120)){
            text<<L"\r\nAttack: "<<wide(attack_classes[prediction.attack_class])<<L" / "<<wide(movement_states[prediction.state])
                <<L"\r\nAttack / threat scores: "<<number(prediction.attack_probability,3)<<L" / "<<(prediction.threat_supported?number(prediction.threat_probability,3):L"unavailable");
            if(prediction.tti_supported)text<<L"\r\nTTI now: "<<number(prediction.tti_ms-(now-prediction.source_ms),0)<<L" +/- "<<number(prediction.tti_uncertainty_ms,0)<<L" ms";
            else text<<L"\r\nTTI: unavailable";
        }else text<<L"\r\nAttack / confidence / TTI: unavailable\r\n"<<wide(latest.model.reason);
        text<<L"\r\nThreat: "<<wide(latest.threat.state)<<L" / "<<wide(latest.threat.reason)
            <<L"\r\nDecision: "<<wide(latest.direction_status)
            <<L"\r\nDodge submitted: "<<latest.input.sent;
        if(debug)text<<L"\r\nSource / delivered age: "<<number(cap.source_age_ms)<<L" / "<<number(cap.delivered_age_ms)<<L" ms"
            <<L"\r\nDecision frame age: "<<(latest.has_frame?number(now-latest.preview.source_ms):L"unavailable")<<L" ms"
            <<L"\r\nSource / GPU FPS: "<<number(cap.capture_fps)<<L" / "<<number(cap.copy_fps)
            <<L"\r\nInference / pipeline: "<<number(prediction.inference_ms)<<L" / "<<number(latest.cpu_ms)<<L" ms"
            <<L"\r\nHistory: "<<latest.model.history_size<<L" / "<<latest.model.temporal_length
            <<L"\r\n"<<cap.detail;
        SetWindowTextW(status,text.str().c_str());
        std::wstring lines;auto recent=dodge.log()->recent();const auto first=recent.size()>8?recent.size()-8:0;
        for(std::size_t i=first;i<recent.size();++i)lines+=wide(recent[i])+L"\r\n";
        SetWindowTextW(events,lines.c_str());SendMessageW(events,EM_SETSEL,lines.size(),lines.size());SendMessageW(events,EM_SCROLLCARET,0,0);
        const auto overlay_text=std::wstring(L"SekiroVisionAI | Auto Dodge ")+(latest.input.enabled?L"ON":L"OFF")+L"\r\n"+wide(latest.threat.state)+L"  "+wide(latest.direction_status);
        overlay.update(game.hwnd,overlay_on&&cap.active&&!closing&&!smoke,overlay_text);
        InvalidateRect(window,&preview,FALSE);
    }
    void paint(){
        PAINTSTRUCT ps{};auto dc=BeginPaint(window,&ps);SetBkMode(dc,TRANSPARENT);auto old_font=SelectObject(dc,font);
        RECT label=preview;label.top-=px(23);label.bottom=preview.top;
        const auto config=dodge.config();
        DrawTextW(dc,config.automatic_roi?L"Combat preview - automatic ROI":L"Debug: drag a manual combat ROI",-1,&label,DT_LEFT|DT_SINGLELINE);
        FillRect(dc,&preview,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if(const auto image=display_frame()){const auto& color=*image;BITMAPINFO info{};
            info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=color.width;info.bmiHeader.biHeight=-color.height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);
            StretchDIBits(dc,preview.left,preview.top,preview.right-preview.left,preview.bottom-preview.top,0,0,color.width,color.height,color.bgra.data(),&info,DIB_RGB_COLORS,SRCCOPY);
            const auto roi=config.automatic_roi?latest.combat_roi:config.roi;
            auto pen=CreatePen(PS_SOLID,px(2),RGB(60,225,145));auto old_pen=SelectObject(dc,pen);auto old_brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
            const int w=preview.right-preview.left,h=preview.bottom-preview.top;
            Rectangle(dc,preview.left+static_cast<int>(roi.left*w),preview.top+static_cast<int>(roi.top*h),preview.left+static_cast<int>(roi.right*w),preview.top+static_cast<int>(roi.bottom*h));
            if(dragging)Rectangle(dc,std::min(drag_start.x,drag_end.x),std::min(drag_start.y,drag_end.y),std::max(drag_start.x,drag_end.x),std::max(drag_start.y,drag_end.y));
            SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(pen);
        }
        RECT note=preview;note.top=preview.bottom+px(10);note.bottom=note.top+px(42);
        const auto readiness=config.detector_mode==0?L"Debug CV heuristic active; scores are not model probabilities.":
            latest.input.detector_ready?L"Arming requires game foreground and fresh capture.":L"Auto Dodge unavailable: gameplay model missing or unsupported.";
        const auto note_text=std::wstring(L"F8 toggles Auto Dodge in Sekiro. F9 immediately stops it.\r\n")+readiness;
        DrawTextW(dc,note_text.c_str(),-1,&note,DT_LEFT|DT_WORDBREAK);
        SelectObject(dc,old_font);EndPaint(window,&ps);
    }
    POINT bound(POINT p)const{p.x=std::clamp(p.x,preview.left,preview.right);p.y=std::clamp(p.y,preview.top,preview.bottom);return p;}
    void mouse_down(POINT p){if(debug&&!dodge.config().automatic_roi&&PtInRect(&preview,p)&&latest.has_frame){dodge.disable();dragging=true;drag_start=drag_end=p;SetCapture(window);}}
    void mouse_move(POINT p){if(dragging){drag_end=bound(p);InvalidateRect(window,&preview,FALSE);}}
    void mouse_up(POINT p){
        if(!dragging)return;drag_end=bound(p);dragging=false;ReleaseCapture();auto config=dodge.config();
        const double w=preview.right-preview.left,h=preview.bottom-preview.top;
        CombatRoi roi{std::clamp((std::min(drag_start.x,drag_end.x)-preview.left)/w,.03,.97),std::clamp((std::min(drag_start.y,drag_end.y)-preview.top)/h,.03,.92),
            std::clamp((std::max(drag_start.x,drag_end.x)-preview.left)/w,.03,.97),std::clamp((std::max(drag_start.y,drag_end.y)-preview.top)/h,.03,.92)};
        if(roi.valid()){config.roi=roi;dodge.configure(config);}InvalidateRect(window,&preview,FALSE);
    }
    void command(int id,int code){
        if(closing)return;
        if(code==CBN_SELCHANGE&&(id==Mode||id==Provider||id==Direction)){
            auto config=dodge.config();const auto selected=SendMessageW(id==Mode?mode:id==Provider?provider:direction,CB_GETCURSEL,0,0);
            if(id==Mode)config.detector_mode=selected==0?1:0;
            if(id==Provider)config.provider=selected==1?"DirectML":selected==2?"CPU":selected==3?"CUDA":"Auto";
            if(id==Direction)config.direction=static_cast<int>(selected)-1;dodge.configure(config);return;
        }
        if(code!=BN_CLICKED)return;
        if(id==Start){discover();if(!game.hwnd||!target_is_current(game))return;dodge.start(game);if(!capture.start(game,false))dodge.stop();}
        else if(id==Stop){dodge.stop();capture.request_stop();}
        else if(id==Toggle)dodge.toggle();
        else if(id==Advanced){debug=SendMessageW(advanced,BM_GETCHECK,0,0)==BST_CHECKED;layout();}
        else if(id==Overlay)overlay_on=SendMessageW(overlay_control,BM_GETCHECK,0,0)==BST_CHECKED;
        else if(id==Record)dodge.recording(SendMessageW(record,BM_GETCHECK,0,0)==BST_CHECKED);
        else if(id==ManualRoi||id==ResetRoi){auto config=dodge.config();config.automatic_roi=id==ResetRoi||SendMessageW(manual_roi,BM_GETCHECK,0,0)!=BST_CHECKED;
            SendMessageW(manual_roi,BM_SETCHECK,config.automatic_roi?BST_UNCHECKED:BST_CHECKED,0);dodge.configure(config);}
        else if(id==OpenLog){auto path=L"\""+dodge.log()->path().wstring()+L"\"";ShellExecuteW(window,L"open",L"notepad.exe",path.c_str(),nullptr,SW_SHOWNORMAL);}
        else if(id==Model){
            dodge.disable();std::wstring path(32768,L'\0');OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;
            dialog.lpstrFilter=L"Temporal ONNX (*.onnx)\0*.onnx\0\0";dialog.lpstrFile=path.data();dialog.nMaxFile=static_cast<DWORD>(path.size());dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
            if(GetOpenFileNameW(&dialog)){path.resize(wcslen(path.c_str()));auto config=dodge.config();config.model_path=path;config.detector_mode=1;SendMessageW(mode,CB_SETCURSEL,0,0);dodge.configure(config);}
        }
        update();
    }
    void close(){closing=true;dodge.stop();capture.request_stop();update();}
};
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wp,LPARAM lp){
    auto app=reinterpret_cast<Application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){app=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    try{if(app)switch(message){
        case WM_CREATE:app->create(window);return 0;
        case WM_SIZE:if(app->events)app->layout();return 0;
        case WM_TIMER:app->update();return 0;
        case WM_PAINT:if(app->events){app->paint();return 0;}break;
        case WM_COMMAND:app->command(LOWORD(wp),HIWORD(wp));return 0;
        case WM_LBUTTONDOWN:app->mouse_down({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
        case WM_MOUSEMOVE:app->mouse_move({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
        case WM_LBUTTONUP:app->mouse_up({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});return 0;
        case WM_CAPTURECHANGED:app->dragging=false;return 0;
        case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={app->px(980),app->px(820)};return 0;
        case WM_DPICHANGED:{app->dpi=HIWORD(wp);app->apply_font();auto r=reinterpret_cast<RECT*>(lp);SetWindowPos(window,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);app->layout();return 0;}
        case WM_CLOSE:app->close();return 0;
        case WM_DESTROY:KillTimer(window,1);PostQuitMessage(app->exit_code);return 0;
    }}catch(...){if(app){app->dodge.stop();app->capture.request_stop();app->exit_code=3;}if(message==WM_CREATE)return -1;
        MessageBoxW(window,L"The application stopped Auto Dodge after an error.",L"SekiroVisionAI",MB_OK|MB_ICONERROR);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int show){
    try{
        if(auto result=input_watchdog_command_line())return *result;
        Application app;app.smoke=std::wstring(command).find(L"--smoke-test")!=std::wstring::npos;
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=instance;wc.lpfnWndProc=procedure;wc.lpszClassName=L"SekiroVisionAI.Application";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        if(!RegisterClassExW(&wc))return 1;
        auto window=CreateWindowExW(0,wc.lpszClassName,L"SekiroVisionAI",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(1080,GetDpiForSystem(),96),MulDiv(860,GetDpiForSystem(),96),nullptr,nullptr,instance,&app);
        if(!window)return 1;ShowWindow(window,app.smoke?SW_HIDE:show);UpdateWindow(window);
        MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}return static_cast<int>(msg.wParam);
    }catch(...){MessageBoxW(nullptr,L"Could not start SekiroVisionAI. Check access to LocalAppData.",L"SekiroVisionAI",MB_OK|MB_ICONERROR);return 1;}
}
