/* SPDX-License-Identifier: GPL-3.0-or-later */
// The Renderer's side of the mako-fg-host protocol, against a fake program, so no MAKO FG file is needed.
#include "mako_fg.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <spawn.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
extern char** environ;
using namespace mako::backend;
#if defined(MAKO_TEST_GAME_THREAD_STORAGE)
// Some games carry over a megabyte of static thread-local storage (Savage Wind: 1.14 MiB), which glibc places inside
// every new thread's stack, the program's watcher included.
thread_local std::array<char,2*1024*1024> gameThreadStorage{};
#endif
namespace {
bool picker(const std::string& name,const std::pair<const std::string&,const std::string&>& ids,const std::optional<std::string>&) {
    return name=="Fake GPU"&&ids.first=="0x1002"&&ids.second=="0x7480";
}
std::unique_ptr<ContextImpl> openContext(MakoFG& host) {
    const auto descriptor=[]{return open("/dev/null",O_RDONLY|O_CLOEXEC);};
    std::array<int,2> sources{descriptor(),descriptor()},outputs{descriptor(),descriptor()};std::array<int,1> sync{descriptor()};
    ls::FileDescriptorScope a{sources},b{outputs},c{sync};
    return host.open(a,b,c,1280,720,FrameEncoding::Sdr8,1.25F,2);
}
template<class Action> std::string failure(Action action) {
    try {action();} catch(const std::exception& error) {return error.what();}
    return {};
}
void useContext() {
    setenv("FAKE_HOST_MODE","",1);
    MakoFG host(picker,"/unused",false);
    assert(host.supportsPackedHdr10());
    auto context=openContext(host);
    const std::array<float,2> two{0.25F,0.5F};const std::array<float,3> three{0.25F,0.5F,0.75F};
    context->scheduleFrames(two);context->scheduleFrameHistory();context->scheduleFrames();
    assert(context->waitForIdle(0));
    context->enableFrameProfiling();
    const auto profile=context->readFrameProfile();
    assert(profile.gpuSpanUs==125&&profile.gpuGeneratedUs.size()==2&&profile.gpuGeneratedUs[1]==7);
    // A failure the host reports is an ordinary error, and the context stays usable.
    assert(failure([&]{context->scheduleFrames(three);})=="fake schedule failure");
    context->scheduleFrames(two);
}
void lostHost() {
    setenv("FAKE_HOST_MODE","crash",1);
    MakoFG host(picker,"/unused",false);
    auto context=openContext(host);
    const std::array<float,3> three{0.25F,0.5F,0.75F};const std::array<float,1> one{0.5F};
    // The game keeps running: every later call fails at once, and closing the context does not hang.
    const auto start=std::chrono::steady_clock::now();
    assert(failure([&]{context->scheduleFrames(three);})=="MAKO FG host stopped responding");
    assert(failure([&]{context->scheduleFrames(one);})=="MAKO FG host stopped responding");
    // A lost host has no work left: gates that wait for idle continue, and nothing fails again.
    assert(context->lost()&&context->waitForIdle(0));
    // A replacement serves new contexts, a second report of the same loss starts nothing,
    // and the limit stops a program that keeps failing from restarting forever.
    const std::array<float,2> two{0.25F,0.5F};
    for(int restart=0;restart<3;restart++) {
        assert(host.restart(*context));
        assert(host.restart(*context));
        auto replacement=openContext(host);
        replacement->scheduleFrames(two);
        assert(failure([&]{replacement->scheduleFrames(three);})=="MAKO FG host stopped responding");
        context=std::move(replacement);
    }
    assert(!host.restart(*context));
    context.reset();
    assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(2));
}
// A swapchain created during an outage starts a replacement, and a replacement that dies is itself replaced.
void outageRecovery() {
    setenv("FAKE_HOST_MODE","crash",1);
    MakoFG host(picker,"/unused",false);
    const std::array<float,3> three{0.25F,0.5F,0.75F};
    const std::array<float,2> two{0.25F,0.5F};
    auto first=openContext(host);
    assert(failure([&]{first->scheduleFrames(three);})=="MAKO FG host stopped responding");
    const auto settled=std::chrono::steady_clock::now();
    while(std::chrono::steady_clock::now()-settled<std::chrono::milliseconds(50))std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto duringOutage=openContext(host);
    duringOutage->scheduleFrames(two);
    assert(failure([&]{duringOutage->scheduleFrames(three);})=="MAKO FG host stopped responding");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // The first context is two programs old, and the program it would find is gone too.
    assert(host.restart(*first));
    auto replacement=openContext(host);
    replacement->scheduleFrames(two);
}
// Selecting HDR10's transport can be the first request a new swapchain sends to a program that has gone.
void hdrDuringOutage() {
    setenv("FAKE_HOST_MODE","crash",1);
    MakoFG host(picker,"/unused",false);
    const std::array<float,3> three{0.25F,0.5F,0.75F};
    auto context=openContext(host);
    assert(failure([&]{context->scheduleFrames(three);})=="MAKO FG host stopped responding");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(host.supportsPackedHdr10());
    auto replacement=openContext(host);
    replacement->scheduleFrames(std::array<float,2>{0.25F,0.5F});
}
// The program is 64-bit for every game, so a 32-bit game can reach it from another driver build.
void driverIdentity() {
    setenv("FAKE_HOST_MODE","",1);
    DriverIdentity same;same.driver.fill(7);same.device.fill(9);
    {MakoFG host(picker,"/unused",false,same);auto context=openContext(host);}
    auto otherDriver=same,otherDevice=same;otherDriver.driver[0]=1;otherDevice.device[0]=1;
    for(const auto& other:{otherDriver,otherDevice})
        assert(failure([&]{MakoFG host(picker,"/unused",false,other);})=="mako-fg-host and the game are using different graphics drivers");
}
// A game's thread can be stuck behind the lost program's timeline, so losing it must not depend on any request.
void lostWhileIdle() {
    setenv("FAKE_HOST_MODE","die-while-idle",1);
    MakoFG host(picker,"/unused",false);
    auto context=openContext(host);
    std::atomic<int> released{0};
    context->setLossHandler([&]{released++;});
    const auto start=std::chrono::steady_clock::now();
    while(!released&&std::chrono::steady_clock::now()-start<std::chrono::seconds(2))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(released==1&&context->lost());
    // A handler set after the loss runs at once, and neither runs twice.
    std::atomic<int> late{0};
    context->setLossHandler([&]{late++;});
    assert(late==1&&released==1);
    context.reset();
}
void refusedHosts(const char* path) {
    for(const char* mode:{"old-protocol","other-layout"}) {
        setenv("FAKE_HOST_MODE",mode,1);
        assert(failure([]{MakoFG host(picker,"/unused",false);})=="MAKO FG host does not match this Renderer");
    }
    setenv("MAKO_FG_HOST","/nonexistent/mako-fg-host",1);
    assert(failure([]{MakoFG host(picker,"/unused",false);})=="Unable to start mako-fg-host");
    setenv("MAKO_FG_HOST",path,1);
}
void leavesNothingBehind() {
    // Each start exits its first process at once, so this process never has a child to reap.
    assert(waitpid(-1,nullptr,WNOHANG)==-1&&errno==ECHILD);
}
// The file the fake program ran as, and its library search path.
std::pair<std::string,std::string> startedAs(const std::filesystem::path& report) {
    std::ifstream file(report);std::pair<std::string,std::string> lines;
    std::getline(file,lines.first);std::getline(file,lines.second);
    return lines;
}
// Runs in this test started again through a dynamic linker: the program must start through the same linker file and
// with this process's library search path, which names the folder given to that linker first.
void startedThroughLinker(const std::filesystem::path& linker,const std::filesystem::path& folder) {
    const auto report=folder/"report";
    setenv("FAKE_HOST_MODE","",1);setenv("FAKE_HOST_REPORT",report.c_str(),1);
    {MakoFG host(picker,"/unused",false);auto context=openContext(host);}
    const auto [file,search]=startedAs(report);
    assert(file==std::filesystem::canonical(linker).string());
    assert(search.starts_with((folder/"lib").string()+":"));
}
// Self-contained AppImages run a game through their own copy of the dynamic linker, which finds the game's graphics
// driver in the AppImage. Another program then finds that driver only when it starts the same way.
void gameLinkers(const char* self,const char* fake) {
#if defined(__x86_64__)
    const auto folder=std::filesystem::temp_directory_path()/("mako-fg-game-linker-"+std::to_string(getpid()));
    std::filesystem::create_directories(folder/"lib");
    const auto report=folder/"report";
    setenv("FAKE_HOST_MODE","",1);setenv("FAKE_HOST_REPORT",report.c_str(),1);
    // A game started normally on the linker the program was built for starts it directly.
    {MakoFG host(picker,"/unused",false);}
    assert(startedAs(report).first==std::filesystem::canonical(fake).string());
    unsetenv("FAKE_HOST_REPORT");
    // A bundled copy shares the system linker's name, so only the mapping tells them apart.
    const auto copy=folder/"lib/ld-linux-x86-64.so.2";
    std::filesystem::copy_file("/lib64/ld-linux-x86-64.so.2",copy);
    std::filesystem::permissions(copy,std::filesystem::perms::owner_all);
    for(const auto& linker:{copy,std::filesystem::path("/lib64/ld-linux-x86-64.so.2")}) {
        std::vector<std::string> command{linker.string(),"--library-path",(folder/"lib").string(),self,fake,"--started-through",
            linker.string(),folder.string()};
        std::vector<char*> arguments;for(auto& part:command)arguments.push_back(part.data());arguments.push_back(nullptr);
        pid_t child{};int status{};
        assert(posix_spawn(&child,arguments[0],nullptr,nullptr,arguments.data(),environ)==0);
        assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    }
    std::filesystem::remove_all(folder);
#else
    static_cast<void>(self);static_cast<void>(fake);
#endif
}
}
int main(int argc,char** argv) {
#if defined(MAKO_TEST_GAME_THREAD_STORAGE)
    gameThreadStorage[0]=1;
#endif
    setenv("MAKO_FG_HOST",argv[1],1);
    unsetenv("MAKO_FG_PACKAGE");
    if(argc==5&&std::string_view(argv[2])=="--started-through") {
        startedThroughLinker(argv[3],argv[4]);
        return 0;
    }
    useContext();
    lostHost();
    outageRecovery();
    hdrDuringOutage();
    driverIdentity();
    lostWhileIdle();
    refusedHosts(argv[1]);
    gameLinkers(std::filesystem::read_symlink("/proc/self/exe").c_str(),argv[1]);
    leavesNothingBehind();
    return 0;
}
