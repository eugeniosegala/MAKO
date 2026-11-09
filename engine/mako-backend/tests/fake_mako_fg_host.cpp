/* SPDX-License-Identifier: GPL-3.0-or-later */
// Stands in for mako-fg-host so the Renderer's side of the protocol can be tested without MAKO FG.
#include "mako_fg_protocol.hpp"
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
using namespace mako::backend::fg_host;
namespace {
constexpr int control=hostDescriptor;
void reply(int socket,uint32_t first=0,uint32_t second=0) {
    Reply message{};message.first=first;message.second=second;send(socket,&message,offsetof(Reply,text),MSG_NOSIGNAL);
}
void refuse(int socket,const char* text) {
    Reply message{};message.failed=1;std::snprintf(message.text,sizeof message.text,"%s",text);
    send(socket,&message,offsetof(Reply,text)+std::strlen(message.text)+1,MSG_NOSIGNAL);
}
void serve(int socket,bool crash) {
    reply(socket,64,36);
    Request request;
    while(recv(socket,&request,sizeof request,0)==ssize_t(sizeof request)) {
        switch(request.op) {
            case Schedule:
                if(request.count==3&&crash)_exit(9);
                if(request.count==3)refuse(socket,"fake schedule failure");
                else reply(socket);
                break;
            case Ready:reply(socket,1);break;
            case ReadProfile: {
                Reply message{};ProfileData data{};data.span=125;data.generatedCount=2;data.generated[1]=7;
                std::memcpy(message.text,&data,sizeof data);send(socket,&message,offsetof(Reply,text)+sizeof data,MSG_NOSIGNAL);break;
            }
            default:reply(socket);
        }
    }
    close(socket);
}
// Records how it was started: the file the kernel ran and its library search path.
void report(const char* path) {
    std::array<char,4096> executable{};const ssize_t length=readlink("/proc/self/exe",executable.data(),executable.size()-1);
    std::string search;
    Dl_serinfo size{};void* self=dlopen(nullptr,RTLD_LAZY);
    if(self&&dlinfo(self,RTLD_DI_SERINFOSIZE,&size)==0) {
        std::vector<std::max_align_t> storage((size.dls_size+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t));
        auto* paths=reinterpret_cast<Dl_serinfo*>(storage.data());
        if(dlinfo(self,RTLD_DI_SERINFOSIZE,paths)==0&&dlinfo(self,RTLD_DI_SERINFO,paths)==0)
            for(unsigned int index=0;index<paths->dls_cnt;index++)search+=(search.empty()?"":":")+std::string(paths->dls_serpath[index].dls_name);
    }
    if(std::FILE* file=std::fopen(path,"w")) {std::fprintf(file,"%.*s\n%s\n",int(length>0?length:0),executable.data(),search.c_str());std::fclose(file);}
}
}
int main() {
    const std::string_view mode=std::getenv("FAKE_HOST_MODE")?std::getenv("FAKE_HOST_MODE"):"";
    if(const char* path=std::getenv("FAKE_HOST_REPORT"))report(path);
    if(fork()!=0)_exit(0);
    std::vector<std::thread> contexts;Request request;
    for(;;) {
        alignas(cmsghdr) char space[CMSG_SPACE(sizeof(int)*(maxOutputs+4))];iovec part{&request,sizeof request};
        msghdr message{};message.msg_iov=&part;message.msg_iovlen=1;message.msg_control=space;message.msg_controllen=sizeof space;
        if(recvmsg(control,&message,0)!=ssize_t(sizeof request))break;
        if(request.op==Hello) {
            Reply answer{};answer.first=mode=="old-protocol"?hostProtocol+1:hostProtocol;answer.second=mode=="other-layout"?1:sizeof(Request);std::snprintf(answer.text,sizeof answer.text,"Fake MAKO FG");
            send(control,&answer,offsetof(Reply,text)+std::strlen(answer.text)+1,MSG_NOSIGNAL);
        } else if(request.op==Devices) {
            Reply answer{};answer.first=1;GpuInfo gpu{};gpu.index=0;gpu.vendor=0x1002;gpu.id=0x7480;
            std::snprintf(gpu.pci,sizeof gpu.pci,"0000:03:00.0");std::snprintf(gpu.name,sizeof gpu.name,"Fake GPU");
            std::memset(gpu.driverUuid,7,sizeof gpu.driverUuid);std::memset(gpu.deviceUuid,9,sizeof gpu.deviceUuid);
            std::memcpy(answer.text,&gpu,sizeof gpu);send(control,&answer,offsetof(Reply,text)+sizeof gpu,MSG_NOSIGNAL);
        } else if(request.op==Supports)reply(control,1);
        else if(request.op==Open) {
            auto* item=CMSG_FIRSTHDR(&message);
            const size_t count=(item->cmsg_len-CMSG_LEN(0))/sizeof(int);std::vector<int> descriptors(count);
            std::memcpy(descriptors.data(),CMSG_DATA(item),count*sizeof(int));
            for(size_t i=1;i<count;i++)close(descriptors[i]);
            if(count!=size_t(request.count)+4) {refuse(descriptors[0],"fake host counted the wrong descriptors");close(descriptors[0]);continue;}
            contexts.emplace_back(serve,descriptors[0],mode=="crash");
            if(mode=="die-while-idle") {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                _exit(9);
            }
        }
    }
    for(auto& context:contexts)context.join();
    return 0;
}
