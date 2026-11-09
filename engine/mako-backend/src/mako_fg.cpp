/* SPDX-License-Identifier: GPL-3.0-or-later */
// MAKO FG runs in mako-fg-host, a separate program installed beside the Renderer. The two share only frames, a
// timeline semaphore and the socket messages declared here; the Renderer links no MAKO FG code. Every request waits
// for its reply, so generated work reaches the GPU in the order a direct call would give it, and a host failure
// reaches the Renderer's existing recovery as an exception. Each context has its own socket, so the frame path takes
// no lock and a context being built never delays one in use.
#include "mako_fg.hpp"
#ifdef MAKO_FG
#include "mako_fg_protocol.hpp"
#include "mako-backend/device_identity.hpp"
#include "mako-common/helpers/paths.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <poll.h>
#include <link.h>
#include <pthread.h>
#include <spawn.h>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
namespace mako::backend {
namespace {
using namespace fg_host;
constexpr int setupSeconds=120,replySeconds=3;
constexpr uint32_t maximumRestarts=3;
constexpr uint64_t longestWaitNs=2000000000ULL;  // Below replySeconds, so a slow wait is an answer rather than a lost host.
constexpr ssize_t replyHeader=offsetof(Reply,text);

void limitReplies(int socket,int seconds) {
    const timeval limit{seconds,0};
    if(setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,&limit,sizeof limit)!=0)throw error("Unable to bound MAKO FG host replies");
}
bool receive(int socket,Reply& reply) {
    ssize_t received;
    do received=recv(socket,&reply,sizeof reply,0); while(received<0&&errno==EINTR);
    return received>=replyHeader;
}
} // namespace
// What a context shares with the thread that watches its program. Application work can wait on that program's
// timeline, so losing it must release that work at once, whatever the game's own threads are doing.
struct HostLink {
    std::mutex lock;
    std::function<void()> release;
    std::atomic<bool> lost{false};
    void lose() {
        lost.store(true,std::memory_order_relaxed);
        const std::lock_guard guard(lock);
        if(release)release();
        release=nullptr;
    }
    void setRelease(std::function<void()> handler) {
        const std::lock_guard guard(lock);
        release=std::move(handler);
        if(release&&lost.load(std::memory_order_relaxed)){release();release=nullptr;}
    }
};
namespace {
// glibc places the process's static thread-local storage inside each new thread's stack and refuses a stack that
// cannot hold it. Some games carry over a megabyte of it (Savage Wind: 1.14 MiB).
size_t staticThreadStorage() {
    size_t total=0;
    dl_iterate_phdr([](dl_phdr_info* module,size_t,void* sum) {
        for(ElfW(Half) index=0;index<module->dlpi_phnum;index++)
            if(const auto& segment=module->dlpi_phdr[index];segment.p_type==PT_TLS)
                *static_cast<size_t*>(sum)+=segment.p_memsz+segment.p_align;
        return 0;
    },&total);
    return total;
}
}
// One sleeping thread per running program. It holds no game signal and needs only a small stack of its own, which
// matters to 32-bit games, and the kernel wakes it when the program ends, however it ends.
struct HostWatch {
    int socket;
    std::atomic<bool> ended{false},stopping{false};
    std::mutex lock;
    std::vector<std::shared_ptr<HostLink>> links;
    pthread_t thread{};
    static void* run(void* argument) {
        auto& watch=*static_cast<HostWatch*>(argument);
        pollfd item{watch.socket,POLLRDHUP,0};
        while(poll(&item,1,-1)<0&&errno==EINTR){}
        watch.ended=true;
        if(watch.stopping)return nullptr;
        const std::lock_guard guard(watch.lock);
        for(const auto& link:watch.links)link->lose();
        return nullptr;
    }
    explicit HostWatch(int socket) : socket(socket) {
        sigset_t all,previous;sigfillset(&all);pthread_attr_t attributes;pthread_attr_init(&attributes);
        pthread_attr_setstacksize(&attributes,256*1024+staticThreadStorage());
        pthread_sigmask(SIG_SETMASK,&all,&previous);
        const int result=pthread_create(&thread,&attributes,run,this);
        pthread_sigmask(SIG_SETMASK,&previous,nullptr);pthread_attr_destroy(&attributes);
        if(result!=0)throw error("Unable to watch mako-fg-host");
    }
    ~HostWatch() {stopping=true;shutdown(socket,SHUT_RDWR);pthread_join(thread,nullptr);}
    HostWatch(const HostWatch&)=delete;
    HostWatch& operator=(const HostWatch&)=delete;
    void add(std::shared_ptr<HostLink> link) {
        const std::lock_guard guard(lock);
        std::erase_if(links,[](const auto& known){return known.use_count()==1;});
        links.push_back(std::move(link));
    }
};
namespace {
// Sends one request and waits for its reply. False means the socket can no longer pair requests with replies.
bool exchange(int socket,const Request& request,Reply& reply) {
    return send(socket,&request,sizeof request,MSG_NOSIGNAL)==ssize_t(sizeof request)&&receive(socket,reply);
}
void requireReply(int socket,const Request& request,Reply& reply) {
    if(!exchange(socket,request,reply))throw error("MAKO FG host stopped responding");
    if(reply.failed)throw error(reply.text);
}
class FGContext final : public ContextImpl {
public:
    // Every descriptor passes to the host, which owns them from here even when the context fails.
    FGContext(int control,std::mutex& controlLock,HostWatch& watch,uint32_t hostGeneration,Request open,ls::FileDescriptorScope& source,ls::FileDescriptorScope& dest,ls::FileDescriptorScope& sync) : capacity(dest.size()),hostGeneration(hostGeneration),link(std::make_shared<HostLink>()) {
        if(capacity==0||capacity>maxOutputs)throw error("Unsupported MAKO FG output count");
        std::array<int,2> sockets{-1,-1};
        if(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,sockets.data())!=0)throw error("Unable to create a MAKO FG context socket");
        socket=sockets[0];
        std::vector<int> descriptors;descriptors.reserve(capacity+4);descriptors.push_back(sockets[1]);
        descriptors.push_back(source.take());descriptors.push_back(source.take());descriptors.push_back(sync.take());
        for(size_t i=0;i<capacity;i++)descriptors.push_back(dest.take());
        struct Sent {std::vector<int>& list;~Sent(){for(int descriptor:list)close(descriptor);}} sent{descriptors};
        open.op=Open;open.count=uint32_t(capacity);
        alignas(cmsghdr) std::array<char,CMSG_SPACE(sizeof(int)*(maxOutputs+4))> space{};iovec part{&open,sizeof open};
        msghdr message{};message.msg_iov=&part;message.msg_iovlen=1;message.msg_control=space.data();message.msg_controllen=CMSG_SPACE(sizeof(int)*descriptors.size());
        auto* item=CMSG_FIRSTHDR(&message);item->cmsg_level=SOL_SOCKET;item->cmsg_type=SCM_RIGHTS;item->cmsg_len=CMSG_LEN(sizeof(int)*descriptors.size());
        std::memcpy(CMSG_DATA(item),descriptors.data(),sizeof(int)*descriptors.size());
        {
            const std::lock_guard lock(controlLock);
            if(sendmsg(control,&message,MSG_NOSIGNAL)!=ssize_t(sizeof open)){close(socket);throw error("MAKO FG host stopped responding");}
        }
        try {
            limitReplies(socket,setupSeconds);
            Reply reply;
            if(!receive(socket,reply))throw error("MAKO FG host stopped responding");
            if(reply.failed)throw error(reply.text);
            analysisWidth=reply.first;analysisHeight=reply.second;
            limitReplies(socket,replySeconds);
            watch.add(link);
        } catch(...) {close(socket);throw;}
    }
    ~FGContext() override {
        // The host closes its end once the context's GPU work has finished, as the direct destructor waited for it.
        if(!lost()) {
            shutdown(socket,SHUT_WR);char ignored;ssize_t received;
            do received=recv(socket,&ignored,1,0); while(received<0&&errno==EINTR);
        }
        close(socket);
    }
    FGContext(const FGContext&)=delete;
    FGContext& operator=(const FGContext&)=delete;
    void setLossHandler(std::function<void()> handler) override {link->setRelease(std::move(handler));}
    bool lost() const override {return link->lost.load(std::memory_order_relaxed);}
    uint32_t generation() const override {return hostGeneration;}
    uint32_t width() const {return analysisWidth;}
    uint32_t height() const {return analysisHeight;}
    void scheduleFrames() override {
        Request request{};request.op=Schedule;request.count=uint32_t(capacity);
        for(size_t i=0;i<capacity;i++)request.times[i]=float(i+1)/float(capacity+1);
        call(request);
    }
    void scheduleFrames(std::span<const float> times) override {
        if(times.size()>maxOutputs)throw error("Too many MAKO FG output timestamps");
        Request request{};request.op=Schedule;request.count=uint32_t(times.size());
        std::copy(times.begin(),times.end(),request.times);call(request);
    }
    void scheduleFrameHistory() override {Request request{};request.op=Schedule;call(request);}
    bool waitForIdle(uint64_t ns) const override {
        // A lost host has no work left, and every gate that waits for idle must be able to continue.
        if(lost())return true;
        Request request{};request.op=Ready;request.timeout=std::min(ns,longestWaitNs);
        Reply reply;call(request,reply);return reply.first!=0;
    }
    void enableFrameProfiling() override {Request request{};request.op=EnableProfiling;call(request);}
    FrameProfile readFrameProfile() const override {
        Request request{};request.op=ReadProfile;Reply reply;call(request,reply);
        ProfileData p;std::memcpy(&p,reply.text,sizeof p);FrameProfile result;
        result.cpuPreviousWaitUs=p.previousWait;result.cpuGenerationSubmitUs=p.submit;
        result.gpuInputConversionUs=p.input;result.gpuMotionEstimationUs=p.analysis;
        result.gpuPrepassUs=p.input+p.analysis;result.gpuSpanUs=p.span;
        result.gpuGeneratedUs.assign(p.generated,p.generated+std::min<size_t>(p.generatedCount,maxOutputs));
        result.gpuSynthesisUs.assign(p.synthesis,p.synthesis+std::min<size_t>(p.synthesisCount,maxOutputs));
        result.gpuOutputConversionUs.assign(p.output,p.output+std::min<size_t>(p.outputCount,maxOutputs));
        result.timestampPeriodNs=p.period;result.timestampValidBits=p.validBits;return result;
    }
private:
    void call(const Request& request) const {Reply reply;call(request,reply);}
    // After a lost reply a late answer would pair with the wrong request, so this context stays failed.
    void call(const Request& request,Reply& reply) const {
        if(lost())throw error("MAKO FG host stopped responding");
        if(!exchange(socket,request,reply)){link->lose();throw error("MAKO FG host stopped responding");}
        if(reply.failed)throw error(reply.text);
    }
    size_t capacity;
    uint32_t hostGeneration;
    int socket{-1};
    uint32_t analysisWidth{},analysisHeight{};
    std::shared_ptr<HostLink> link;
};
// Every MAKO exchange encoding, numbered as the host expects.
int32_t fgEncoding(FrameEncoding encoding) {
    switch(encoding) {
        case FrameEncoding::Sdr8: return 0;
        case FrameEncoding::SdrHighPrecision: return 1;
        case FrameEncoding::Hdr10Pq: return 2;
        case FrameEncoding::Hdr10PqPacked: return 3;
        case FrameEncoding::ScRgbLinear: return 4;
    }
    throw error("Unknown MAKO FG frame encoding");
}
const char* encodingName(FrameEncoding encoding) {
    switch(encoding) {
        case FrameEncoding::Sdr8: return "sdr-8-bit";
        case FrameEncoding::SdrHighPrecision: return "sdr-high-precision";
        case FrameEncoding::Hdr10Pq: return "hdr10-pq";
        case FrameEncoding::Hdr10PqPacked: return "hdr10-pq-packed";
        case FrameEncoding::ScRgbLinear: return "scrgb-linear";
    }
    return "unknown";
}
// One 64-bit program serves every game. It installs in mako-fg/ beside the 64-bit Renderer library: lib/ beside a tool's
// bin/ or a native lib32/, and lib64/ above a Flatpak extension's lib/i386-linux-gnu/. Those two come first so that the
// 32-bit program an earlier build left in lib32/mako-fg/ cannot take over from the current one.
std::filesystem::path hostProgram() {
    if(const char* value=std::getenv("MAKO_FG_HOST");value&&*value)return value;
    Dl_info info{};std::error_code problem;std::filesystem::path self;
    if(dladdr(reinterpret_cast<void*>(&hostProgram),&info)!=0&&info.dli_fname)self=std::filesystem::canonical(info.dli_fname,problem);
    if(self.empty())self=std::filesystem::read_symlink("/proc/self/exe",problem);
    const auto directory=self.parent_path();
    for(const auto& candidate:{directory.parent_path()/"lib/mako-fg/mako-fg-host",directory.parent_path().parent_path()/"lib64/mako-fg/mako-fg-host",directory/"mako-fg/mako-fg-host"})
        if(std::filesystem::is_regular_file(candidate,problem))return candidate;
    throw error("MAKO FG is not installed: mako-fg-host is missing");
}
// The host's environment, taken while the caller's layer-disabling overrides apply so a later restart keeps them.
std::vector<std::string> hostEnvironment() {
    std::vector<std::string> variables{"DISABLE_VK_LAYER_VALVE_steam_overlay_1=1","DISABLE_VK_LAYER_VALVE_steam_fossilize_1=1"};
    for(char** entry=environ;entry&&*entry;entry++)if(std::string_view(*entry).rfind("LD_PRELOAD=",0)!=0)variables.emplace_back(*entry);
    return variables;
}
// The file mapped at a base address, as the kernel names it.
std::string mappedFile(uintptr_t base) {
    std::ifstream maps("/proc/self/maps");std::string line;
    while(std::getline(maps,line)) {
        unsigned long start{},offset{};int path{-1};
        if(std::sscanf(line.c_str(),"%lx-%*x %*s %lx %*s %*s %n",&start,&offset,&path)<2||path<0)continue;
        if(start!=base||offset!=0)continue;
        const std::string file=line.substr(size_t(path));
        return file.starts_with('/')&&!file.ends_with(" (deleted)")?file:std::string();
    }
    return {};
}
// A game that its own dynamic linker started, as self-contained AppImages are, found its graphics driver through that
// linker's search path. The program started on its own would search elsewhere and find another driver or none, so it
// starts the way the game did: through the linker the game runs on, with the game's library search path. A game on
// the linker the program was built for, started normally, gets an empty launcher; Proton, Flatpak and native games
// are such games. The program is 64-bit, so a 32-bit game's linker cannot start it.
std::vector<std::string> hostLauncher() {
#if defined(__x86_64__)
    constexpr const char* programLinker="/lib64/ld-linux-x86-64.so.2";
    Dl_info inLinker{};
    if(void* symbol=dlsym(RTLD_DEFAULT,"__tls_get_addr");!symbol||dladdr(symbol,&inLinker)==0)return {};
    // The linker's own record names the path a program asks for, which a bundled copy shares; the mapping names the file.
    const auto linker=mappedFile(reinterpret_cast<uintptr_t>(inLinker.dli_fbase));
    struct stat mapped{},expected{},executable{};
    if(linker.empty()||stat(linker.c_str(),&mapped)!=0)return {};
    const auto same=[&](const struct stat& other){return other.st_dev==mapped.st_dev&&other.st_ino==mapped.st_ino;};
    const bool ownLinker=stat(programLinker,&expected)!=0||!same(expected);
    const bool startedByLinker=stat("/proc/self/exe",&executable)==0&&same(executable);
    if(!ownLinker&&!startedByLinker)return {};
    void* game=dlopen(nullptr,RTLD_LAZY);
    Dl_serinfo size{};
    if(!game||dlinfo(game,RTLD_DI_SERINFOSIZE,&size)!=0)return {};
    std::vector<std::max_align_t> storage((size.dls_size+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t));
    auto* search=reinterpret_cast<Dl_serinfo*>(storage.data());
    if(dlinfo(game,RTLD_DI_SERINFOSIZE,search)!=0||dlinfo(game,RTLD_DI_SERINFO,search)!=0)return {};
    std::string path;
    for(unsigned int index=0;index<search->dls_cnt;index++)
        path+=(path.empty()?"":":")+std::string(search->dls_serpath[index].dls_name);
    return {linker,"--library-path",path};
#else
    return {};
#endif
}
// Starts the host with a clean signal state. Its first process exits at once, so this process keeps no child.
int startHost(const std::filesystem::path& program,const std::optional<std::filesystem::path>& package,std::vector<std::string> variables,
        std::vector<std::string> launcher) {
    std::array<int,2> sockets{-1,-1};
    if(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,sockets.data())!=0)throw error("Unable to create the MAKO FG host socket");
    std::vector<char*> environment;environment.reserve(variables.size()+1);
    for(auto& variable:variables)environment.push_back(variable.data());
    environment.push_back(nullptr);
    std::string name=program.string(),directory=package?package->string():std::string();
    std::vector<char*> arguments;
    for(auto& part:launcher)arguments.push_back(part.data());
    arguments.push_back(name.data());
    if(package)arguments.push_back(directory.data());
    arguments.push_back(nullptr);
    posix_spawn_file_actions_t actions;posix_spawnattr_t attributes;sigset_t nothing,everything;
    sigemptyset(&nothing);sigfillset(&everything);
    posix_spawn_file_actions_init(&actions);posix_spawn_file_actions_adddup2(&actions,sockets[1],hostDescriptor);
    posix_spawnattr_init(&attributes);posix_spawnattr_setsigmask(&attributes,&nothing);posix_spawnattr_setsigdefault(&attributes,&everything);
    posix_spawnattr_setflags(&attributes,POSIX_SPAWN_SETSIGMASK|POSIX_SPAWN_SETSIGDEF);
    pid_t process{};const int result=posix_spawn(&process,arguments[0],&actions,&attributes,arguments.data(),environment.data());
    posix_spawn_file_actions_destroy(&actions);posix_spawnattr_destroy(&attributes);close(sockets[1]);
    if(result!=0){close(sockets[0]);throw error("Unable to start mako-fg-host");}
    while(waitpid(process,nullptr,0)<0&&errno==EINTR){}
    return sockets[0];
}
}
MakoFG::~MakoFG() {watch.reset();if(control>=0)close(control);}
bool MakoFG::supportsPackedHdr10() {
    Request request{};request.op=Supports;request.device=deviceIndex;request.encoding=fgEncoding(FrameEncoding::Hdr10PqPacked);
    const std::lock_guard lock(controlLock);ensureHost();Reply reply;requireReply(control,request,reply);return reply.first!=0;
}
void MakoFG::connect() {
    const int host=startHost(hostProgram(),package,environment,launcher);
    try {
        limitReplies(host,setupSeconds);
        Request request{};request.op=Hello;Reply reply;requireReply(host,request,reply);
        if(reply.first!=hostProtocol||reply.second!=sizeof(Request))throw error("MAKO FG host does not match this Renderer");
        label=reply.text;
    } catch(...) {close(host);throw;}
    control=host;hostGeneration++;
    watch=std::make_unique<HostWatch>(control);
}
MakoFG::MakoFG(const DevicePicker& picker,const std::filesystem::path& path,bool half,const std::optional<DriverIdentity>& gameDriver) : environment(hostEnvironment()),launcher(hostLauncher()),halfStorage(half) {
    // MAKO_FG_PACKAGE (an absolute package directory, such as a release's model/ folder) overrides the installed release.
    if(ls::makoFgPackageOverride()) {
        if(!path.is_absolute())throw error("MAKO_FG_PACKAGE must be an absolute local package directory");
        // Pin the directory for this process, including later swapchain recreation.
        package=std::filesystem::canonical(path);
        for(const auto* file:{"weights.bin","ops.spv","transport.spv"})if(!std::filesystem::is_regular_file(*package/file))throw error("MAKO FG package is incomplete");
    }
    if(!launcher.empty())std::clog<<"MAKO Renderer: mako-fg operation=host-launch linker="<<launcher[0]<<" library_path="<<launcher[2]<<"\n";
    // The watching thread runs this library's code, so it stays loaded until the process ends.
    Dl_info self{};
    if(dladdr(reinterpret_cast<void*>(&hostProgram),&self)!=0&&self.dli_fname)static_cast<void>(dlopen(self.dli_fname,RTLD_NOW|RTLD_NOLOAD|RTLD_NODELETE));
    connect();
    try {
        Request request{};request.op=Devices;Reply reply;requireReply(control,request,reply);
        std::array<GpuInfo,8> gpus;std::memcpy(gpus.data(),reply.text,std::min<size_t>(reply.first,gpus.size())*sizeof(GpuInfo));
        // The picker compares these with the layer's own identity of the game's GPU, so
        // use the shared formats; the runtime reports the PCI address in hexadecimal.
        for(size_t i=0;i<std::min<size_t>(reply.first,gpus.size());i++) {
            const auto& gpu=gpus[i];auto vendor=deviceIdText(gpu.vendor),device=deviceIdText(gpu.id);
            if(!picker(gpu.name,{vendor,device},pciAddressFromHexText(gpu.pci)))continue;
            // The program is 64-bit whatever the game is, and a 32-bit driver can be a different build from the 64-bit one.
            if(gameDriver&&(std::memcmp(gameDriver->driver.data(),gpu.driverUuid,16)!=0||std::memcmp(gameDriver->device.data(),gpu.deviceUuid,16)!=0))
                throw error("mako-fg-host and the game are using different graphics drivers");
            deviceIndex=gpu.index;break;
        }
        if(deviceIndex<0)throw error("No matching MAKO FG GPU");
        limitReplies(control,replySeconds);
    } catch(...) {watch.reset();close(control);control=-1;throw;}
    std::clog<<"MAKO Renderer: fg-model selected="<<label<<(package?"":" source=release")<<" precision="<<(halfStorage?"fp16-storage-fp32-arithmetic":"fp32")<<"\n";
}
bool MakoFG::restart(const ContextImpl& lostContext) {
    const std::lock_guard lock(controlLock);
    // Another swapchain may already have replaced this program. A hung one still counts as running, so a context
    // of the current program always replaces it.
    if(lostContext.generation()!=hostGeneration&&watch&&!watch->ended)return true;
    return replaceHost();
}
bool MakoFG::replaceHost() {
    if(restarts==maximumRestarts) {
        if(!restartLimitReported)std::clog<<"MAKO Renderer: mako-fg operation=host-restart-limit restarts="<<restarts<<"\n";
        restartLimitReported=true;return false;
    }
    restarts++;
    // A hung program ends by itself once this socket closes.
    watch.reset();
    if(control>=0)close(control);
    control=-1;connect();limitReplies(control,replySeconds);
    std::clog<<"MAKO Renderer: mako-fg operation=host-restarted restarts="<<restarts<<"\n";
    return true;
}
// A swapchain created while the program is down must not lose generation until the game restarts.
void MakoFG::ensureHost() {
    if((!watch||watch->ended)&&!replaceHost())throw error("MAKO FG host stopped responding");
}
std::unique_ptr<ContextImpl> MakoFG::open(ls::FileDescriptorScope& a,ls::FileDescriptorScope& b,ls::FileDescriptorScope& sync,uint32_t w,uint32_t h,FrameEncoding encoding,float flow,int motionPeriod) {
    if(!std::isfinite(flow)||flow<1.0f||flow>4.0f)throw error("MAKO FG requires Flow Scale from 25% to 100%");
    {
        const std::lock_guard lock(controlLock);
        ensureHost();
    }
    Request request{};request.device=deviceIndex;request.encoding=fgEncoding(encoding);request.period=motionPeriod;
    request.half=halfStorage?1:0;request.width=w;request.height=h;request.flow=1.0f/flow;
    const auto outputs=b.size();
    auto context=std::make_unique<FGContext>(control,controlLock,*watch,hostGeneration,request,a,b,sync);
    std::clog<<"MAKO Renderer: mako-fg operation=context-open width="<<w<<" height="<<h<<" outputs="<<outputs<<" encoding="<<encodingName(encoding)<<" precision="<<(halfStorage?"fp16-storage-fp32-arithmetic":"fp32")<<" model="<<label<<" motion_period="<<motionPeriod<<" flow_scale="<<1.0f/flow<<" analysis_width="<<context->width()<<" analysis_height="<<context->height()<<" memory_accounting=unavailable\n";
    return context;
}
}
#endif
