// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine_host.hpp"
#include "engine.h"
#include "utils/crypto.hpp"
#include "wasm-rt-exceptions.h"
#include "wasm-rt-impl.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <stdexcept>
#include <sys/stat.h>

namespace
{
using Stream = std::unique_ptr<FILE, decltype(&std::fclose)>;
struct Descriptor
{
    Stream file{nullptr, std::fclose};
    std::string path;
    uint64_t offset = 0;
    unsigned type = 0; // WASI 2 = character device, 3 = directory, 4 = regular file.
};

struct Host
{
    w2c_engine engine{};
    std::string root;
    std::span<const uint8_t> input;
    std::vector<std::string> args;
    forge::EngineResult result;
    std::string diagnostic;
    size_t hashOffset = 0;
    pksm::crypto::SHA256 sha256;
    std::array<u8,32> digest{};
    std::array<Descriptor, 64> descriptors;
    int exitCode = -1;
    uint8_t* bytes(uint32_t offset, uint64_t count)
    {
        auto* memory = w2c_engine_memory(&engine);
        if (uint64_t(offset) + count > memory->size)
            throw std::runtime_error("Engine memory range exceeded.");
        return memory->data + offset;
    }
    template<typename T> void put(uint32_t offset, T value) { std::memcpy(bytes(offset,sizeof(T)), &value, sizeof(T)); }
    uint32_t word(uint32_t offset) { uint32_t value; std::memcpy(&value,bytes(offset,4),4); return value; }
    std::string path(uint32_t fd, uint32_t ptr, uint32_t size)
    {
        if (fd >= descriptors.size() || descriptors[fd].type != 3 || size > 512) return {};
        std::string name(reinterpret_cast<char*>(bytes(ptr,size)),size);
        while (name.starts_with("./")) name.erase(0,2);
        if (name.find("..") != name.npos || name.find_first_of(":/\\") == 0 ||
            name.find(':') != name.npos || name.find('\\') != name.npos || name.find('\0') != name.npos) return {};
        if (descriptors[fd].path == "managed") name = "managed/" + name;
        return name;
    }
    std::string readPath(const std::string& name) const
    {
        if (name.starts_with("mgdb-") && name.ends_with(".pkl") && name.find('/')==name.npos)
            return "sdmc:/switch/PKForge/media/events/mgdb/" + name.substr(5);
        return root + "/" + name;
    }
    uint64_t length(const Descriptor& d) const
    {
        if (d.path == "input.sav") return input.size();
        if (d.path == "sha256.result") return digest.size();
        if (d.path == "changed.sav") return result.changed.size();
        struct stat st{};
        if (d.file && !fstat(fileno(d.file.get()), &st)) return st.st_size;
        return 0;
    }
    void statBuffer(uint32_t out, unsigned type, uint64_t length)
    {
        std::memset(bytes(out,64),0,64);
        put<uint8_t>(out+16,type); put<uint64_t>(out+24,1); put<uint64_t>(out+32,length);
    }
};
struct GuestExit {};
}
struct w2c_wasi__snapshot__preview1 { Host* host; };
#define WASI(name) w2c_wasi__snapshot__preview1_##name
using Context = w2c_wasi__snapshot__preview1;
u32 WASI(args_sizes_get)(Context* c,u32 n,u32 size)
{
    auto& h=*c->host; u32 total=0;
    for (const auto& arg:h.args) total+=arg.size()+1;
    h.put(n,u32(h.args.size())); h.put(size,total); return 0;
}
u32 WASI(args_get)(Context* c,u32 offsets,u32 data)
{
    auto& h=*c->host;
    for (const auto& arg:h.args) {
        h.put(offsets,data); offsets+=4;
        std::memcpy(h.bytes(data,arg.size()+1),arg.c_str(),arg.size()+1); data+=arg.size()+1;
    }
    return 0;
}
u32 WASI(environ_sizes_get)(Context* c,u32 n,u32 size) { c->host->put(n,0u); c->host->put(size,0u); return 0; }
u32 WASI(environ_get)(Context*,u32,u32) { return 0; }
u32 WASI(clock_res_get)(Context* c,u32,u32 out) { c->host->put<uint64_t>(out,1000); return 0; }
u32 WASI(clock_time_get)(Context* c,u32 id,u64,u32 out)
{
    using namespace std::chrono;
    auto stamp=id==0 ? duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count()
                     : duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    c->host->put<uint64_t>(out,stamp); return 0;
}
#ifdef __SWITCH2__
extern "C" void randomGet(void*, size_t);
#endif
u32 WASI(random_get)(Context* c,u32 ptr,u32 count)
{
    auto* bytes=c->host->bytes(ptr,count);
#ifdef __SWITCH2__
    randomGet(bytes,count);
#else
    std::random_device rng;
    for (u32 i=0;i<count;i++) bytes[i]=static_cast<uint8_t>(rng());
#endif
    return 0;
}
u32 WASI(fd_prestat_get)(Context* c,u32 fd,u32 out)
{
    if (fd!=3) return 8;
    c->host->put(out,0u); c->host->put(out+4,1u); return 0;
}
u32 WASI(fd_prestat_dir_name)(Context* c,u32 fd,u32 out,u32 len)
{
    if (fd!=3 || len<1) return 8;
    c->host->put<uint8_t>(out,'.'); return 0;
}
u32 WASI(path_open)(Context* c,u32 fd,u32,u32 ptr,u32 size,u32 flags,u64,u64,u32,u32 out)
{
    auto& h=*c->host; auto name=h.path(fd,ptr,size);
    if (name.empty()) return 44;
    Descriptor d; d.path=name; d.type=4;
    if (name=="." || name=="managed" || name=="tmp") d.type=3;
    else if (name=="sha256.stream") { h.sha256.reinitialize(); h.hashOffset=0; }
    else if (name=="sha256.result") { /* Read-only digest from the last completed stream. */ }
    else if (name=="changed.sav") { if (flags&8) h.result.changed.clear(); }
    else if (name!="input.sav") {
        if (flags & (1|8)) return 69;
        d.file.reset(std::fopen(h.readPath(name).c_str(),"rb"));
        if (!d.file) return 44;
    }
    for (u32 i=4;i<h.descriptors.size();i++) if (!h.descriptors[i].type) {
        h.descriptors[i]=std::move(d); h.put(out,i); return 0;
    }
    return 33;
}
u32 WASI(path_filestat_get)(Context* c,u32 fd,u32,u32 ptr,u32 size,u32 out)
{
    auto& h=*c->host; auto name=h.path(fd,ptr,size);
    if (name=="input.sav") h.statBuffer(out,4,h.input.size());
    else if (name=="sha256.stream" || name=="sha256.result") h.statBuffer(out,4,name=="sha256.result"?h.digest.size():0);
    else if (name=="changed.sav" && !h.result.changed.empty()) h.statBuffer(out,4,h.result.changed.size());
    else if (name=="." || name=="managed" || name=="tmp") h.statBuffer(out,3,0);
    else {
        struct stat st{};
        if (name.empty() || ::stat(h.readPath(name).c_str(),&st)) return 44;
        h.statBuffer(out,S_ISDIR(st.st_mode)?3:4,st.st_size);
    }
    return 0;
}
u32 WASI(fd_fdstat_get)(Context* c,u32 fd,u32 out)
{
    auto& h=*c->host;
    if (fd>=h.descriptors.size() || !h.descriptors[fd].type) return 8;
    std::memset(h.bytes(out,24),0,24); h.put<uint8_t>(out,h.descriptors[fd].type);
    h.put(out+8,~uint64_t(0)); h.put(out+16,~uint64_t(0)); return 0;
}
u32 WASI(fd_filestat_get)(Context* c,u32 fd,u32 out)
{
    auto& h=*c->host;
    if (fd>=h.descriptors.size() || !h.descriptors[fd].type) return 8;
    h.statBuffer(out,h.descriptors[fd].type,h.length(h.descriptors[fd])); return 0;
}
u32 WASI(fd_close)(Context* c,u32 fd)
{
    if (fd<4 || fd>=c->host->descriptors.size()) return 8;
    if (c->host->descriptors[fd].path=="sha256.stream") c->host->digest=c->host->sha256.finish();
    c->host->descriptors[fd]=Descriptor{}; return 0;
}
u32 WASI(fd_seek)(Context* c,u32 fd,u64 move,u32 whence,u32 out)
{
    auto& h=*c->host;
    if (fd>=h.descriptors.size() || h.descriptors[fd].type!=4) return 8;
    auto& d=h.descriptors[fd]; uint64_t base=whence==0?0:whence==1?d.offset:h.length(d);
    int64_t offset=static_cast<int64_t>(move);
    if (whence>2 || offset < -static_cast<int64_t>(base) || offset>0x7fffffff) return 28;
    d.offset=base+offset; h.put(out,d.offset); return 0;
}
u32 WASI(fd_tell)(Context* c,u32 fd,u32 out) { return WASI(fd_seek)(c,fd,0,1,out); }
static u32 transfer(Context* c,u32 fd,u32 vec,u32 count,u64 offset,u32 out,bool write)
{
    auto& h=*c->host;
    if (fd>=h.descriptors.size() || !h.descriptors[fd].type || count>65536) return 8;
    auto& d=h.descriptors[fd]; u32 total=0;
    h.bytes(vec,uint64_t(count)*8);
    for (u32 i=0;i<count;i++,vec+=8) {
        u32 len=h.word(vec+4); auto* dest=h.bytes(h.word(vec),len);
        if (write && (fd==1 || fd==2)) {
            auto& text=fd==1?h.result.text:h.diagnostic;
            if (text.size()+len>4*1024*1024) return 27;
            text.append(reinterpret_cast<char*>(dest),len);
        } else if (write && d.path=="sha256.stream") {
            if (offset!=h.hashOffset) return 28;
            h.sha256.update({dest,len}); h.hashOffset+=len;
        } else if (!write && d.path=="sha256.result") {
            len=std::min<uint64_t>(len,h.digest.size()-std::min<uint64_t>(offset,h.digest.size()));
            if (len) std::memcpy(dest,h.digest.data()+offset,len);
        } else if (write && d.path=="changed.sav") {
            if (offset>32*1024*1024 || len>32*1024*1024-offset) return 27;
            if (offset+len>h.result.changed.size()) h.result.changed.resize(offset+len);
            std::memcpy(h.result.changed.data()+offset,dest,len);
        } else if (write) return 69;
        else if (d.path=="input.sav") {
            len=std::min<uint64_t>(len,h.input.size()-std::min<uint64_t>(offset,h.input.size()));
            if (len) std::memcpy(dest,h.input.data()+offset,len);
        } else if (d.file) {
            if (offset>0x7fffffff || std::fseek(d.file.get(),offset,SEEK_SET)) return 29;
            len=std::fread(dest,1,len,d.file.get());
        } else len=0;
        total+=len; offset+=len;
    }
    h.put(out,total); return 0;
}
u32 WASI(fd_pread)(Context* c,u32 fd,u32 v,u32 n,u64 off,u32 out) { return transfer(c,fd,v,n,off,out,false); }
u32 WASI(fd_pwrite)(Context* c,u32 fd,u32 v,u32 n,u64 off,u32 out) { return transfer(c,fd,v,n,off,out,true); }
static u32 sequential(Context* c,u32 fd,u32 v,u32 n,u32 out,bool write)
{
    if (fd>=c->host->descriptors.size()) return 8;
    auto& offset=c->host->descriptors[fd].offset;
    auto err=transfer(c,fd,v,n,offset,out,write); if (!err) offset+=c->host->word(out); return err;
}
u32 WASI(fd_read)(Context* c,u32 fd,u32 v,u32 n,u32 out) { return sequential(c,fd,v,n,out,false); }
u32 WASI(fd_write)(Context* c,u32 fd,u32 v,u32 n,u32 out) { return sequential(c,fd,v,n,out,true); }
u32 WASI(fd_sync)(Context*,u32) { return 0; }
void WASI(proc_exit)(Context* c,u32 code) { c->host->exitCode=static_cast<int>(code); throw GuestExit{}; }

#include "unused_imports.inc"

forge::EngineResult forge::runEngine(const std::string& resourceRoot, std::span<const uint8_t> input,
    const std::vector<std::string>& arguments)
{
    Host h; h.root=resourceRoot; h.input=input; h.args=arguments;
    h.args.insert(h.args.begin(),"dotnet");
    for (int fd=0;fd<4;++fd) h.descriptors[fd].type=fd==3?3:2;
    Context imports{&h};
    wasm_rt_init();
    auto release=[&](w2c_engine* e){ wasm2c_engine_free(e); wasm_rt_free(); };
    std::unique_ptr<w2c_engine,decltype(release)> cleanup(&h.engine,release);
    if (auto trap=wasm_rt_impl_try())
        throw std::runtime_error(std::string("Engine stopped: ")+wasm_rt_strerror(static_cast<wasm_rt_trap_t>(trap))+" "+h.diagnostic.substr(0,180));
    wasm2c_engine_instantiate(&h.engine,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,
        nullptr,nullptr,nullptr,nullptr,nullptr,&imports);
    try { w2c_engine_0x5Fstart(&h.engine); h.exitCode=0; }
    catch (const GuestExit&) {}
    if (h.exitCode!=0)
        throw std::runtime_error((h.diagnostic.empty()?h.result.text:h.diagnostic).substr(0,1000));
    return std::move(h.result);
}

#include <atomic>
#include <exception>
#ifdef __SWITCH2__
#include <switch.h>
#else
#include <thread>
#endif
struct forge::EngineJob::State
{
    std::shared_ptr<const std::vector<std::uint8_t>> input;
    std::vector<std::string> arguments;
    EngineResult result;
    std::exception_ptr error;
    std::atomic<bool> done{false};
#ifdef __SWITCH2__
    Thread worker{};
#else
    std::thread worker;
#endif
    static void execute(void* opaque)
    {
        auto& self = *static_cast<State*>(opaque);
        try
        {
#ifdef __SWITCH2__
            constexpr auto root = "romfs:/engine";
#else
            constexpr auto root = "romfs/engine";
#endif
            self.result = runEngine(root, *self.input, self.arguments);
        }
        catch (...) { self.error = std::current_exception(); }
        self.done.store(true, std::memory_order_release);
    }
};
forge::EngineJob::EngineJob(std::shared_ptr<const std::vector<std::uint8_t>> input, std::vector<std::string> arguments)
    : state(std::make_unique<State>())
{
    state->input = std::move(input);
    state->arguments = std::move(arguments);
#ifdef __SWITCH2__
    if (R_FAILED(threadCreate(&state->worker, State::execute, state.get(), nullptr, 8*1024*1024, 0x30, -2)))
        throw std::runtime_error("Could not allocate the engine worker.");
    if (R_FAILED(threadStart(&state->worker))) {
        threadClose(&state->worker);
        throw std::runtime_error("Could not start the engine worker.");
    }
#else
    state->worker = std::thread(State::execute, state.get());
#endif
}
forge::EngineJob::~EngineJob()
{
#ifdef __SWITCH2__
    threadWaitForExit(&state->worker);
    threadClose(&state->worker);
#else
    state->worker.join();
#endif
}
bool forge::EngineJob::ready() const { return state->done.load(std::memory_order_acquire); }
forge::EngineResult forge::EngineJob::take()
{
    if (!ready()) throw std::logic_error("Engine is busy.");
    if (state->error) std::rethrow_exception(state->error);
    return std::move(state->result);
}
