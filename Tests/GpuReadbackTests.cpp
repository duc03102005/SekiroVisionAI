#include <SekiroVisionAI/CaptureEngine.h>
#include <iostream>
int main(){
    std::wstring error;
    if(!sekiro::gpu_readback_self_test(error)){std::wcerr<<error<<L'\n';return 1;}
    std::cout<<"D3D11 WARP: BGRA color and grayscale references match for native, 1080p, 4:3, portrait and odd-sized frames; shared color ownership preserved. Not a game benchmark.\n";
}
