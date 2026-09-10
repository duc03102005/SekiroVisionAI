#include <SekiroVisionAI/SampleRecorder.h>
#include <chrono>
#include <iostream>
int main(){
    const auto path=std::filesystem::temp_directory_path()/("svai-recorder-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string detail;const bool result=sekiro::sample_recorder_self_test(path,detail);
    std::cout<<detail<<'\n';std::error_code error;std::filesystem::remove_all(path,error);return result?0:1;
}
