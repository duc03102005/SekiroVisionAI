#include <SekiroVisionAI/CaptureEngine.h>
#include <iostream>
int main(){
    std::wstring error;
    if(!sekiro::gpu_readback_self_test(error)){std::wcerr<<error<<L'\n';return 1;}
    std::cout<<"D3D11 WARP: GPU shader/downsample/staging readback matches grayscale quadrant reference. Not a game benchmark.\n";
}
