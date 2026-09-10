#include <SekiroVisionAI/AutoDodge.h>
#include <shellapi.h>
#include <windowsx.h>
#include <array>
#include <iomanip>
#include <sstream>

namespace {
using namespace sekiro;
constexpr int windows_id=201,refresh_id=202,start_id=203,stop_id=204,disable_id=205,sensitivity_id=206,direction_id=207,logs_id=208,folder_id=209,roi_id=210;
std::wstring wide(const std::string& value){return {value.begin(),value.end()};} // UI event/reason codes are ASCII.
std::wstring fixed(double value,int precision=2){std::wostringstream s;s<<std::fixed<<std::setprecision(precision)<<value;return s.str();}

class Application {
public:
    AutoDodge dodge; // Must outlive capture callbacks.
    CaptureEngine capture;
    HWND window{},heading{},windows{},refresh{},start{},stop{},disable{},sensitivity{},direction{},openlog{},folder{},roi_reset{},metrics{},events{};
    HFONT font{};
    UINT dpi{96};
    std::vector<WindowTarget> targets;
    MvpSnapshot latest;
    CombatRoi roi;
    RECT preview{};
    bool closing{},dragging{},smoke{};
    POINT drag_start{},drag_end{};
    double launched{qpc_ms()};
    int exit_code{};
    ~Application(){if(font)DeleteObject(font);}
    int px(int n)const{return MulDiv(n,static_cast<int>(dpi),96);}
    HWND control(const wchar_t* klass,const wchar_t* title,DWORD style,int id){
        return CreateWindowExW(0,klass,title,WS_VISIBLE|WS_CHILD|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    }
    void create(HWND hwnd){
        window=hwnd;dpi=GetDpiForWindow(hwnd);roi=dodge.config().roi;
        heading=control(L"STATIC",L"SekiroVisionAI  |  Runnable Auto Dodge MVP\r\nF8: toggle in game   |   F9: disable + release   |   F10: one manual test Dodge (when enabled)",0,0);
        windows=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,windows_id);
        refresh=control(L"BUTTON",L"Refresh",WS_TABSTOP,refresh_id);
        start=control(L"BUTTON",L"Start capture",WS_TABSTOP,start_id);
        stop=control(L"BUTTON",L"Stop capture",WS_TABSTOP,stop_id);
        disable=control(L"BUTTON",L"Disable (F9)",WS_TABSTOP,disable_id);
        sensitivity=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,sensitivity_id);
        direction=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,direction_id);
        roi_reset=control(L"BUTTON",L"Reset ROI",WS_TABSTOP,roi_id);
        openlog=control(L"BUTTON",L"Open log",WS_TABSTOP,logs_id);
        folder=control(L"BUTTON",L"Config / logs folder",WS_TABSTOP,folder_id);
        metrics=control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|WS_VSCROLL,0);
        events=control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,0);
        for(auto h:{heading,windows,refresh,start,stop,disable,sensitivity,direction,roi_reset,openlog,folder,metrics,events})
            if(!h)throw std::runtime_error("Could not create MVP controls");
        for(auto label:{L"Conservative",L"Balanced",L"Responsive"})SendMessageW(sensitivity,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        for(auto label:{L"Shift / player direction",L"Left (A + Shift)",L"Right (D + Shift)",L"Back (S + Shift)",L"Forward (W + Shift)"})SendMessageW(direction,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
        auto config=dodge.config();SendMessageW(sensitivity,CB_SETCURSEL,config.preset,0);SendMessageW(direction,CB_SETCURSEL,config.direction,0);
        capture.set_frame_sink([this](const SmallFrame& frame){dodge.submit(frame);},[this]{dodge.discontinuity();});
        apply_font();layout();refresh_windows();update();SetTimer(window,1,100,nullptr);
    }
    void apply_font(){
        auto next=CreateFontW(-px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        for(auto h:{heading,windows,refresh,start,stop,disable,sensitivity,direction,roi_reset,openlog,folder,metrics,events})SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if(font)DeleteObject(font);font=next;
    }
    void layout(){
        RECT client{};GetClientRect(window,&client);const int width=MulDiv(client.right,96,static_cast<int>(dpi)),height=MulDiv(client.bottom,96,static_cast<int>(dpi));
        auto move=[&](HWND h,int x,int y,int w,int z){MoveWindow(h,px(x),px(y),px(w),px(z),TRUE);};
        move(heading,18,12,width-36,48);move(windows,18,67,width-145,220);move(refresh,width-115,66,96,30);
        move(start,18,108,115,32);move(stop,142,108,115,32);move(disable,266,108,115,32);
        move(sensitivity,394,109,150,180);move(direction,555,109,220,190);move(roi_reset,786,108,105,32);
        const int image_width=std::min(512,(width-54)*54/100),image_height=image_width*9/16;
        preview={px(18),px(174),px(18+image_width),px(174+image_height)};
        move(metrics,image_width+36,160,width-image_width-54,image_height+50);
        const int log_top=std::max(556,174+image_height+98);
        move(openlog,18,log_top-36,110,29);move(folder,140,log_top-36,170,29);
        move(events,18,log_top,width-36,std::max(80,height-log_top-18));
        InvalidateRect(window,nullptr,TRUE);
    }
    void refresh_windows(){
        targets=find_sekiro_windows();SendMessageW(windows,CB_RESETCONTENT,0,0);
        for(auto& target:targets){auto label=target.title+L"  [sekiro.exe PID "+std::to_wstring(target.pid)+L"]";SendMessageW(windows,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
        if(targets.empty())SendMessageW(windows,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Launch Sekiro, then Refresh."));
        SendMessageW(windows,CB_SETCURSEL,0,0);
    }
    void update(){
        auto cap=capture.snapshot();latest=dodge.snapshot();
        if(smoke&&!closing&&qpc_ms()-launched>3000){
            exit_code=latest.input.hotkeys_ready&&latest.input.watchdog_ready?0:2;
            dodge.log()->emit("UI_SMOKE_RESULT",exit_code==0?"Controls, runtime, hotkeys and watchdog initialized; no input sent":"Hotkey/watchdog initialization unavailable");
            close();return;
        }
        if(closing&&!cap.active){DestroyWindow(window);return;}
        EnableWindow(start,!cap.active&&!targets.empty()&&!closing);EnableWindow(stop,cap.active&&!closing);
        EnableWindow(windows,!cap.active&&!closing);EnableWindow(refresh,!cap.active&&!closing);
        EnableWindow(disable,latest.input.enabled&&!closing);
        SetWindowTextW(window,latest.input.enabled?L"SekiroVisionAI - AUTO DODGE ON":L"SekiroVisionAI - Auto Dodge OFF");
        auto config=dodge.config();
        std::wostringstream text;
        text<<(latest.input.enabled?L"AUTO DODGE: ON":L"AUTO DODGE: OFF")<<L"  |  "<<wide(latest.input.reason)
            <<L"\r\nCapture: "<<state_name(cap.state)<<L"  "<<fixed(cap.capture_fps)<<L" FPS"
            <<L"\r\n"<<cap.adapter<<L"\r\n"<<cap.detail
            <<L"\r\n\r\nThreat: "<<wide(latest.threat.state)<<L"  |  "<<wide(latest.threat.reason)
            <<L"\r\nHeuristic score: "<<fixed(latest.motion.score)<<L"  entry / exit: "<<fixed(config.threat.enter_score)<<L" / "<<fixed(config.threat.exit_score)
            <<L"\r\nQuality: "<<fixed(latest.motion.confidence)<<L"  threshold: "<<fixed(config.threat.min_confidence)
            <<L"\r\nROI change: "<<fixed(latest.motion.changed_fraction*100)<<L"%  flow: "<<fixed(latest.motion.flow)
            <<L"\r\nCamera dx/dy: "<<fixed(latest.motion.camera_dx,0)<<L" / "<<fixed(latest.motion.camera_dy,0)<<L"  accel: "<<fixed(latest.motion.acceleration)
            <<L"\r\nCV: "<<fixed(latest.cpu_ms)<<L" ms  source age now: "<<fixed(latest.has_frame?qpc_ms()-latest.preview.source_ms:0)<<L" ms"
            <<L"\r\nProcessed / replaced: "<<latest.processed<<L" / "<<latest.replaced
            <<L"\r\nThreats: "<<latest.threats<<L"  Dodge sent: "<<latest.input.sent<<L"  suppressed: "<<latest.input.rejected
            <<L"\r\nArming: "<<config.threat.dwell_ms<<L" ms  cooldown: "<<config.threat.cooldown_ms<<L" ms"
            <<L"\r\nWatchdog: "<<(latest.input.watchdog_ready?L"ready":L"unavailable")<<L"  Logs: "<<(dodge.log()->healthy()?L"writing":L"write failed")
            <<L"\r\n\r\nScores are CV heuristics. They are not trained attack probabilities.";
        SetWindowTextW(metrics,text.str().c_str());
        std::wstring log_text;
        auto recent=dodge.log()->recent();const std::size_t begin=recent.size()>12?recent.size()-12:0;
        for(std::size_t i=begin;i<recent.size();++i)log_text+=wide(recent[i])+L"\r\n";
        SetWindowTextW(events,log_text.c_str());SendMessageW(events,EM_SETSEL,log_text.size(),log_text.size());SendMessageW(events,EM_SCROLLCARET,0,0);
        InvalidateRect(window,&preview,FALSE);
    }
    void paint(){
        PAINTSTRUCT paint{};HDC dc=BeginPaint(window,&paint);SetBkMode(dc,TRANSPARENT);
        const auto old_font=SelectObject(dc,font);
        RECT label=preview;label.top-=px(24);label.bottom=preview.top-px(2);
        DrawTextW(dc,L"Drag a box around the enemy/body/weapon. Exclude Wolf and HUD.",-1,&label,DT_LEFT|DT_SINGLELINE);
        FillRect(dc,&preview,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if(latest.has_frame){
            std::array<std::uint32_t,vision_width*vision_height> pixels{};
            for(std::size_t i=0;i<pixels.size();++i){auto v=latest.preview.gray[i];pixels[i]=0xff000000u|(v<<16)|(v<<8)|v;}
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=vision_width;info.bmiHeader.biHeight=-vision_height;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            StretchDIBits(dc,preview.left,preview.top,preview.right-preview.left,preview.bottom-preview.top,0,0,vision_width,vision_height,pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
        }
        auto outline=CreatePen(PS_SOLID,px(2),RGB(45,235,128));auto old_pen=SelectObject(dc,outline);auto old_brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
        const int w=preview.right-preview.left,h=preview.bottom-preview.top;
        Rectangle(dc,preview.left+static_cast<int>(roi.left*w),preview.top+static_cast<int>(roi.top*h),preview.left+static_cast<int>(roi.right*w),preview.top+static_cast<int>(roi.bottom*h));
        if(dragging)Rectangle(dc,std::min(drag_start.x,drag_end.x),std::min(drag_start.y,drag_end.y),std::max(drag_start.x,drag_end.x),std::max(drag_start.y,drag_end.y));
        SelectObject(dc,old_pen);DeleteObject(outline);SelectObject(dc,old_brush);
        RECT note=preview;note.top=preview.bottom+px(8);note.bottom=note.top+px(38);
        DrawTextW(dc,L"Lock on to the enemy. Return to Sekiro and press F8.\r\nF10 tests input only; automatic Dodge requires a detected motion threat.",-1,&note,DT_LEFT|DT_WORDBREAK);
        SelectObject(dc,old_font);EndPaint(window,&paint);
    }
    POINT bound(POINT p)const{p.x=std::clamp(p.x,preview.left,preview.right);p.y=std::clamp(p.y,preview.top,preview.bottom);return p;}
    void mouse_down(POINT p){if(PtInRect(&preview,p)&&latest.has_frame){dodge.disable();dragging=true;drag_start=drag_end=p;SetCapture(window);}}
    void mouse_move(POINT p){if(dragging){drag_end=bound(p);InvalidateRect(window,&preview,FALSE);}}
    void mouse_up(POINT p){
        if(!dragging)return;drag_end=bound(p);dragging=false;ReleaseCapture();
        auto config=dodge.config();const double w=preview.right-preview.left,h=preview.bottom-preview.top;
        CombatRoi next{std::clamp((std::min(drag_start.x,drag_end.x)-preview.left)/w,0.03,0.97),std::clamp((std::min(drag_start.y,drag_end.y)-preview.top)/h,0.03,0.92),
            std::clamp((std::max(drag_start.x,drag_end.x)-preview.left)/w,0.03,0.97),std::clamp((std::max(drag_start.y,drag_end.y)-preview.top)/h,0.03,0.92)};
        if(next.valid()){config.roi=next;roi=next;dodge.configure(config);}InvalidateRect(window,&preview,FALSE);
    }
    void command(int id,int code){
        if(closing)return;
        if(code==CBN_SELCHANGE&&(id==sensitivity_id||id==direction_id)){
            auto config=dodge.config();
            if(id==direction_id)config.direction=static_cast<int>(SendMessageW(direction,CB_GETCURSEL,0,0));
            else{
                config.preset=static_cast<int>(SendMessageW(sensitivity,CB_GETCURSEL,0,0));config.threat={};
                if(config.preset==0){config.threat.enter_score=0.72;config.threat.exit_score=0.48;config.threat.min_confidence=0.62;config.threat.dwell_ms=55;config.threat.cooldown_ms=800;}
                if(config.preset==2){config.threat.enter_score=0.52;config.threat.exit_score=0.34;config.threat.min_confidence=0.50;config.threat.dwell_ms=30;}
            }
            dodge.configure(config);return;
        }
        if(code!=BN_CLICKED)return;
        if(id==refresh_id)refresh_windows();
        else if(id==start_id){
            const auto selected=SendMessageW(windows,CB_GETCURSEL,0,0);
            if(selected<0||static_cast<std::size_t>(selected)>=targets.size())return;
            const auto& target=targets[static_cast<std::size_t>(selected)];
            if(!target_is_current(target)){refresh_windows();return;}
            dodge.start(target);if(!capture.start(target,false))dodge.stop();
        }else if(id==stop_id){dodge.stop();capture.request_stop();}
        else if(id==disable_id)dodge.disable();
        else if(id==roi_id){auto config=dodge.config();roi={};config.roi=roi;dodge.configure(config);}
        else if(id==logs_id){auto path=L"\""+dodge.log()->path().wstring()+L"\"";ShellExecuteW(window,L"open",L"notepad.exe",path.c_str(),nullptr,SW_SHOWNORMAL);}
        else if(id==folder_id)ShellExecuteW(window,L"open",dodge.config_path().parent_path().c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    }
    void close(){closing=true;dodge.stop();capture.request_stop();update();}
};

LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
    auto* app=reinterpret_cast<Application*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(message==WM_NCCREATE){app=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    try{if(app)switch(message){
        case WM_CREATE:app->create(window);return 0;
        case WM_SIZE:if(app->events)app->layout();return 0;
        case WM_TIMER:app->update();return 0;
        case WM_PAINT:if(app->events){app->paint();return 0;}break;
        case WM_COMMAND:app->command(LOWORD(wparam),HIWORD(wparam));return 0;
        case WM_LBUTTONDOWN:app->mouse_down({GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)});return 0;
        case WM_MOUSEMOVE:app->mouse_move({GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)});return 0;
        case WM_LBUTTONUP:app->mouse_up({GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)});return 0;
        case WM_GETMINMAXINFO:{auto* info=reinterpret_cast<MINMAXINFO*>(lparam);info->ptMinTrackSize={app->px(1040),app->px(760)};return 0;}
        case WM_DPICHANGED:{app->dpi=HIWORD(wparam);app->apply_font();auto* rect=reinterpret_cast<RECT*>(lparam);SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);app->layout();return 0;}
        case WM_CLOSE:app->close();return 0;
        case WM_DESTROY:KillTimer(window,1);PostQuitMessage(app->exit_code);return 0;
    }}catch(...){if(app){app->dodge.stop();app->capture.request_stop();app->exit_code=3;}if(message==WM_CREATE)return -1;
        MessageBoxW(window,L"The application encountered an error. Auto Dodge has been disabled.",L"SekiroVisionAI",MB_OK|MB_ICONERROR);return 0;}
    return DefWindowProcW(window,message,wparam,lparam);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int show){
    try{
        if(auto result=input_watchdog_command_line())return *result;
        Application app;app.smoke=std::wstring(command).find(L"--smoke-test")!=std::wstring::npos;
        WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.hInstance=instance;cls.lpfnWndProc=procedure;cls.lpszClassName=L"SekiroVisionAI.AutoDodgeMVP";
        cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        if(!RegisterClassExW(&cls))return 1;
        HWND window=CreateWindowExW(0,cls.lpszClassName,L"SekiroVisionAI - Auto Dodge MVP",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
            CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(1140,GetDpiForSystem(),96),MulDiv(850,GetDpiForSystem(),96),nullptr,nullptr,instance,&app);
        if(!window)return 1;
        ShowWindow(window,app.smoke?SW_HIDE:show);UpdateWindow(window);
        MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        return static_cast<int>(msg.wParam);
    }catch(...){MessageBoxW(nullptr,L"Could not start Auto Dodge MVP. Check access to LocalAppData.",L"SekiroVisionAI",MB_OK|MB_ICONERROR);return 1;}
}
