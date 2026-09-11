#pragma once
#include <windows.h>
#include <string>

// A separate, non-activating status window; never participates in game input.
class StatusOverlay {
    HWND window_{};
    std::wstring text_;
    static LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
        auto self=reinterpret_cast<StatusOverlay*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<StatusOverlay*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(msg==WM_NCHITTEST)return HTTRANSPARENT;
        if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        if(msg==WM_PAINT&&self){
            PAINTSTRUCT ps{};auto dc=BeginPaint(hwnd,&ps);RECT r{};GetClientRect(hwnd,&r);
            auto bg=CreateSolidBrush(RGB(0,0,0));FillRect(dc,&r,bg);DeleteObject(bg);
            SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(110,255,175));
            auto font=CreateFontW(-MulDiv(16,static_cast<int>(GetDpiForWindow(hwnd)),96),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
            auto old=SelectObject(dc,font);r.left=10;r.top=6;
            DrawTextW(dc,self->text_.c_str(),-1,&r,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
            SelectObject(dc,old);DeleteObject(font);EndPaint(hwnd,&ps);return 0;
        }
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
public:
    ~StatusOverlay(){if(window_)DestroyWindow(window_);}
    void update(HWND game,bool visible,const std::wstring& value) {
        if(!visible||!game||GetForegroundWindow()!=game||IsIconic(game)){
            if(window_)ShowWindow(window_,SW_HIDE);return;
        }
        if(!window_){
            WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=procedure;wc.lpszClassName=L"SekiroVisionAI.StatusOverlay";
            if(!RegisterClassExW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return;
            window_=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT|WS_EX_LAYERED,
                wc.lpszClassName,L"SekiroVisionAI status",WS_POPUP,0,0,420,100,nullptr,nullptr,wc.hInstance,this);
            if(!window_)return;
            SetLayeredWindowAttributes(window_,RGB(0,0,0),235,LWA_COLORKEY|LWA_ALPHA);
            SetWindowDisplayAffinity(window_,0x00000011); // WDA_EXCLUDEFROMCAPTURE where supported.
        }
        RECT r{};GetClientRect(game,&r);POINT origin{r.left,r.top};ClientToScreen(game,&origin);
        const int dpi=static_cast<int>(GetDpiForWindow(game));
        SetWindowPos(window_,HWND_TOPMOST,origin.x+MulDiv(16,dpi,96),origin.y+MulDiv(16,dpi,96),MulDiv(460,dpi,96),MulDiv(100,dpi,96),SWP_NOACTIVATE|SWP_SHOWWINDOW);
        if(value!=text_){text_=value;InvalidateRect(window_,nullptr,FALSE);}
    }
};
