#include "rpc.h"
#include "pluginmain.h"
#include "../third_party/json.hpp"

#include <vector>
#include <cstdint>

using json = nlohmann::json;

namespace
{
    const char* kHex = "0123456789abcdef";

    std::string ToHex(const uint8_t* data, size_t size)
    {
        std::string s;
        s.resize(size * 2);
        for (size_t i = 0; i < size; i++)
        {
            s[i * 2] = kHex[data[i] >> 4];
            s[i * 2 + 1] = kHex[data[i] & 0xF];
        }
        return s;
    }

    bool FromHex(const std::string& s, std::vector<uint8_t>& out)
    {
        if (s.size() % 2 != 0)
            return false;
        out.resize(s.size() / 2);
        auto nib = [](char c) -> int
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i < out.size(); i++)
        {
            int hi = nib(s[i * 2]), lo = nib(s[i * 2 + 1]);
            if (hi < 0 || lo < 0)
                return false;
            out[i] = (uint8_t)((hi << 4) | lo);
        }
        return true;
    }

    // Resolve an address/value: accepts a number or any x64dbg expression string.
    bool ResolveValue(const json& params, const char* key, duint& out)
    {
        if (!params.contains(key))
            return false;
        const json& v = params[key];
        if (v.is_number_unsigned())
        {
            out = (duint)v.get<uint64_t>();
            return true;
        }
        if (v.is_number_integer())
        {
            out = (duint)v.get<int64_t>();
            return true;
        }
        if (v.is_string())
        {
            std::string expr = v.get<std::string>();
            bool ok = false;
            duint val = DbgEval(expr.c_str(), &ok);
            if (!ok)
                return false;
            out = val;
            return true;
        }
        return false;
    }

    std::string HexAddr(duint v)
    {
        char buf[32];
        sprintf_s(buf, sizeof(buf), "0x%llX", (unsigned long long)v);
        return buf;
    }

    // ---- method implementations ---------------------------------------------

    json MethodPing(const json&)
    {
        json r;
        r["plugin"] = PLUGIN_NAME;
        r["version"] = PLUGIN_VERSION;
#ifdef _WIN64
        r["arch"] = "x64";
#else
        r["arch"] = "x86";
#endif
        return r;
    }

    json MethodStatus(const json&)
    {
        json r;
        bool dbg = DbgIsDebugging();
        bool running = dbg && DbgIsRunning();
        r["debugging"] = dbg;
        r["running"] = running;
        r["paused"] = dbg && !running;
        // While running, the CPU context (cip/registers/stack) is the last captured
        // snapshot and is NOT live; pause the debuggee for coherent state.
        r["stateLive"] = !running;

        if (!dbg)
        {
            r["state"] = "no target";
            return r;
        }
        r["state"] = running ? "running" : "paused";

        // Process identity.
        DWORD pid = DbgGetProcessId();
        if (pid)
        {
            r["pid"] = (uint32_t)pid;
            duint peb = DbgGetPebAddress(pid);
            if (peb)
                r["peb"] = HexAddr(peb);
        }

        // Main module = what we're attached to.
        Script::Module::ModuleInfo main{};
        if (Script::Module::GetMainModuleInfo(&main))
        {
            json m;
            m["name"] = main.name;
            m["path"] = main.path;
            m["base"] = HexAddr(main.base);
            m["entry"] = HexAddr(main.entry);
            r["target"] = m;
        }

        // Command line of the debuggee, if available.
        size_t cbsize = 0;
        if (DbgFunctions()->GetCmdline(nullptr, &cbsize) && cbsize)
        {
            std::vector<char> buf(cbsize + 1, 0);
            if (DbgFunctions()->GetCmdline(buf.data(), &cbsize))
                r["commandLine"] = buf.data();
        }

        // Current thread + instruction pointer (live only when paused).
        THREADLIST tl{};
        DbgGetThreadList(&tl);
        if (tl.count && tl.CurrentThread >= 0 && tl.CurrentThread < tl.count)
            r["currentThreadId"] = (uint32_t)tl.list[tl.CurrentThread].BasicInfo.ThreadId;
        r["threadCount"] = tl.count;
        if (tl.list)
            BridgeFree(tl.list);

        bool ok = false;
        duint cip = DbgEval("cip", &ok);
        if (ok)
        {
            r["cip"] = HexAddr(cip);
            char mod[MAX_MODULE_SIZE] = "";
            if (DbgGetModuleAt(cip, mod) && mod[0])
                r["module"] = mod;
            char label[MAX_LABEL_SIZE] = "";
            if (DbgGetLabelAt(cip, SEG_DEFAULT, label) && label[0])
                r["label"] = label;
            BASIC_INSTRUCTION_INFO ins{};
            DbgDisasmFastAt(cip, &ins);
            if (ins.size)
                r["instruction"] = ins.instruction;
        }
        return r;
    }

    json MethodExec(const json& p)
    {
        if (!p.contains("cmd") || !p["cmd"].is_string())
            throw std::runtime_error("missing 'cmd'");
        std::string cmd = p["cmd"].get<std::string>();
        bool ok = DbgCmdExecDirect(cmd.c_str());
        json r;
        r["executed"] = ok;
        return r;
    }

    json MethodEval(const json& p)
    {
        if (!p.contains("expr") || !p["expr"].is_string())
            throw std::runtime_error("missing 'expr'");
        std::string expr = p["expr"].get<std::string>();
        bool ok = false;
        duint val = DbgEval(expr.c_str(), &ok);
        json r;
        r["valid"] = ok;
        if (ok)
        {
            r["value"] = (uint64_t)val;
            r["hex"] = HexAddr(val);
        }
        return r;
    }

    json MethodReadMemory(const json& p)
    {
        duint addr = 0;
        if (!ResolveValue(p, "addr", addr))
            throw std::runtime_error("invalid 'addr'");
        size_t size = p.value("size", (size_t)0);
        if (size == 0 || size > 16 * 1024 * 1024)
            throw std::runtime_error("'size' must be 1..16MiB");

        std::vector<uint8_t> buf(size);
        if (!DbgMemRead(addr, buf.data(), size))
            throw std::runtime_error("DbgMemRead failed (unreadable memory?)");

        json r;
        r["addr"] = HexAddr(addr);
        r["size"] = size;
        r["data"] = ToHex(buf.data(), size);
        return r;
    }

    json MethodWriteMemory(const json& p)
    {
        duint addr = 0;
        if (!ResolveValue(p, "addr", addr))
            throw std::runtime_error("invalid 'addr'");
        if (!p.contains("data") || !p["data"].is_string())
            throw std::runtime_error("missing 'data' (hex string)");
        std::vector<uint8_t> bytes;
        if (!FromHex(p["data"].get<std::string>(), bytes))
            throw std::runtime_error("'data' is not valid hex");
        if (bytes.empty())
            throw std::runtime_error("'data' is empty");
        if (!DbgMemWrite(addr, bytes.data(), bytes.size()))
            throw std::runtime_error("DbgMemWrite failed");

        json r;
        r["addr"] = HexAddr(addr);
        r["written"] = bytes.size();
        return r;
    }

    json MethodRegisters(const json&)
    {
        if (!DbgIsDebugging())
            throw std::runtime_error("not debugging");

        static const char* common[] = {
            "cax", "cbx", "ccx", "cdx", "csi", "cdi", "cbp", "csp", "cip"
        };
#ifdef _WIN64
        static const char* extra[] = { "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };
#endif
        json regs = json::object();
        for (const char* name : common)
        {
            bool ok = false;
            duint v = DbgEval(name, &ok);
            if (ok)
                regs[name] = HexAddr(v);
        }
#ifdef _WIN64
        for (const char* name : extra)
        {
            bool ok = false;
            duint v = DbgEval(name, &ok);
            if (ok)
                regs[name] = HexAddr(v);
        }
#endif
        {
            bool ok = false;
            duint v = DbgEval("eflags", &ok);
            if (ok)
                regs["eflags"] = HexAddr(v);
        }
        json r;
        bool running = DbgIsRunning();
        r["running"] = running;
        r["stateLive"] = !running; // false => values are a stale snapshot (debuggee is running)
        r["registers"] = regs;
        return r;
    }

    json MethodDisasm(const json& p)
    {
        duint addr = 0;
        if (!ResolveValue(p, "addr", addr))
            throw std::runtime_error("invalid 'addr'");
        int count = (int)p.value("count", 10);
        if (count < 1 || count > 2048)
            throw std::runtime_error("'count' must be 1..2048");

        json list = json::array();
        duint cur = addr;
        for (int i = 0; i < count; i++)
        {
            BASIC_INSTRUCTION_INFO info{};
            DbgDisasmFastAt(cur, &info);
            if (info.size == 0)
                break;
            json ins;
            ins["addr"] = HexAddr(cur);
            ins["size"] = info.size;
            ins["text"] = info.instruction;
            if (info.branch)
            {
                ins["branch"] = true;
                ins["call"] = info.call;
                if (info.addr)
                    ins["target"] = HexAddr(info.addr);
            }
            list.push_back(ins);
            cur += info.size;
        }
        json r;
        r["instructions"] = list;
        return r;
    }

    json MethodMemMap(const json&)
    {
        if (!DbgIsDebugging())
            throw std::runtime_error("not debugging");
        MEMMAP map{};
        if (!DbgMemMap(&map))
            throw std::runtime_error("DbgMemMap failed");

        json pages = json::array();
        for (int i = 0; i < map.count; i++)
        {
            const MEMPAGE& pg = map.page[i];
            json e;
            e["base"] = HexAddr((duint)pg.mbi.BaseAddress);
            e["size"] = (uint64_t)pg.mbi.RegionSize;
            e["protect"] = (uint32_t)pg.mbi.Protect;
            e["state"] = (uint32_t)pg.mbi.State;
            e["type"] = (uint32_t)pg.mbi.Type;
            e["info"] = pg.info;
            pages.push_back(e);
        }
        if (map.page)
            BridgeFree(map.page);

        json r;
        r["pages"] = pages;
        return r;
    }

    json MethodBpList(const json&)
    {
        BPMAP list{};
        int count = DbgGetBpList(bp_none, &list);
        json bps = json::array();
        for (int i = 0; i < count; i++)
        {
            const BRIDGEBP& bp = list.bp[i];
            json e;
            e["addr"] = HexAddr(bp.addr);
            e["enabled"] = bp.enabled;
            e["active"] = bp.active;
            e["type"] = (int)bp.type;
            e["singleshoot"] = bp.singleshoot;
            e["hitCount"] = (uint32_t)bp.hitCount;
            if (bp.name[0]) e["name"] = bp.name;
            if (bp.mod[0]) e["module"] = bp.mod;
            if (bp.breakCondition[0]) e["breakCondition"] = bp.breakCondition;
            bps.push_back(e);
        }
        if (list.bp)
            BridgeFree(list.bp);

        json r;
        r["breakpoints"] = bps;
        return r;
    }

    json MethodProcessList(const json&)
    {
        DBGPROCESSINFO* entries = nullptr;
        int count = 0;
        if (!DbgFunctions()->GetProcessList(&entries, &count))
            throw std::runtime_error("GetProcessList failed");

        json procs = json::array();
        for (int i = 0; i < count; i++)
        {
            json e;
            e["pid"] = (uint32_t)entries[i].dwProcessId;
            e["exe"] = entries[i].szExeFile;
            if (entries[i].szExeMainWindowTitle[0])
                e["title"] = entries[i].szExeMainWindowTitle;
            if (entries[i].szExeArgs[0])
                e["args"] = entries[i].szExeArgs;
            procs.push_back(e);
        }
        if (entries)
            BridgeFree(entries);

        json r;
        r["processes"] = procs;
        return r;
    }

    json MethodThreads(const json&)
    {
        if (!DbgIsDebugging())
            throw std::runtime_error("not debugging");
        THREADLIST list{};
        DbgGetThreadList(&list);

        json threads = json::array();
        for (int i = 0; i < list.count; i++)
        {
            const THREADALLINFO& t = list.list[i];
            json e;
            e["number"] = t.BasicInfo.ThreadNumber;
            e["id"] = (uint32_t)t.BasicInfo.ThreadId;
            e["cip"] = HexAddr(t.ThreadCip);
            e["entry"] = HexAddr(t.BasicInfo.ThreadStartAddress);
            e["teb"] = HexAddr(t.BasicInfo.ThreadLocalBase);
            e["suspendCount"] = (int)t.SuspendCount;
            e["lastError"] = (uint32_t)t.LastError;
            e["current"] = (i == list.CurrentThread);
            if (t.BasicInfo.threadName[0])
                e["name"] = t.BasicInfo.threadName;
            threads.push_back(e);
        }
        if (list.list)
            BridgeFree(list.list);

        json r;
        r["threads"] = threads;
        r["currentThread"] = list.CurrentThread;
        return r;
    }

    json MethodModules(const json&)
    {
        if (!DbgIsDebugging())
            throw std::runtime_error("not debugging");
        ListInfo li{};
        if (!Script::Module::GetList(&li))
            throw std::runtime_error("Module::GetList failed");

        auto* mods = (Script::Module::ModuleInfo*)li.data;
        json modules = json::array();
        for (int i = 0; i < li.count; i++)
        {
            json e;
            e["base"] = HexAddr(mods[i].base);
            e["size"] = (uint64_t)mods[i].size;
            e["entry"] = HexAddr(mods[i].entry);
            e["sections"] = mods[i].sectionCount;
            e["name"] = mods[i].name;
            e["path"] = mods[i].path;
            modules.push_back(e);
        }
        if (li.data)
            BridgeFree(li.data);

        json r;
        r["modules"] = modules;
        return r;
    }

    json MethodModuleSections(const json& p)
    {
        if (!p.contains("module") || !p["module"].is_string())
            throw std::runtime_error("missing 'module'");
        std::string mod = p["module"].get<std::string>();
        ListInfo li{};
        if (!Script::Module::SectionListFromName(mod.c_str(), &li))
            throw std::runtime_error("module not found or has no sections");

        auto* secs = (Script::Module::ModuleSectionInfo*)li.data;
        json sections = json::array();
        for (int i = 0; i < li.count; i++)
        {
            json e;
            e["addr"] = HexAddr(secs[i].addr);
            e["size"] = (uint64_t)secs[i].size;
            e["name"] = secs[i].name;
            sections.push_back(e);
        }
        if (li.data)
            BridgeFree(li.data);

        json r;
        r["sections"] = sections;
        return r;
    }

    json MethodModuleExports(const json& p)
    {
        if (!p.contains("module") || !p["module"].is_string())
            throw std::runtime_error("missing 'module'");
        std::string mod = p["module"].get<std::string>();
        std::string filter = p.value("filter", std::string());
        size_t maxCount = p.value("max", (size_t)200);

        Script::Module::ModuleInfo info{};
        if (!Script::Module::InfoFromName(mod.c_str(), &info))
            throw std::runtime_error("module not found");

        ListInfo li{};
        if (!Script::Module::GetExports(&info, &li))
            throw std::runtime_error("GetExports failed");

        auto* exp = (Script::Module::ModuleExport*)li.data;
        json exports = json::array();
        for (int i = 0; i < li.count && exports.size() < maxCount; i++)
        {
            std::string name = exp[i].name;
            if (!filter.empty() && name.find(filter) == std::string::npos)
                continue;
            json e;
            e["ordinal"] = (uint64_t)exp[i].ordinal;
            e["va"] = HexAddr(exp[i].va);
            e["rva"] = HexAddr(exp[i].rva);
            e["name"] = name;
            if (exp[i].forwarded && exp[i].forwardName[0])
                e["forward"] = exp[i].forwardName;
            exports.push_back(e);
        }
        int total = li.count;
        if (li.data)
            BridgeFree(li.data);

        json r;
        r["exports"] = exports;
        r["total"] = total;
        r["truncated"] = (exports.size() >= maxCount);
        return r;
    }

    json MethodCallStack(const json&)
    {
        if (!DbgIsDebugging())
            throw std::runtime_error("not debugging");
        DBGCALLSTACK cs{};
        DbgFunctions()->GetCallStack(&cs);

        json frames = json::array();
        for (int i = 0; i < cs.total; i++)
        {
            json e;
            e["addr"] = HexAddr(cs.entries[i].addr);   // stack address of the return pointer
            e["from"] = HexAddr(cs.entries[i].from);   // caller
            e["to"] = HexAddr(cs.entries[i].to);       // callee
            if (cs.entries[i].comment[0])
                e["comment"] = cs.entries[i].comment;
            frames.push_back(e);
        }
        if (cs.entries)
            BridgeFree(cs.entries);

        json r;
        r["frames"] = frames;
        return r;
    }

    json MethodReadStack(const json& p)
    {
        if (!DbgIsDebugging())
            throw std::runtime_error("not debugging");
        int count = (int)p.value("count", 16);
        if (count < 1 || count > 256)
            throw std::runtime_error("'count' must be 1..256");

        bool ok = false;
        duint csp = DbgEval("csp", &ok);
        if (!ok)
            throw std::runtime_error("cannot read csp");

        const size_t ptr = sizeof(duint);
        json entries = json::array();
        for (int i = 0; i < count; i++)
        {
            duint addr = csp + (duint)(i * ptr);
            duint value = 0;
            if (!DbgMemRead(addr, &value, ptr))
                break;
            json e;
            e["addr"] = HexAddr(addr);
            e["value"] = HexAddr(value);

            char label[MAX_LABEL_SIZE] = "";
            if (DbgGetLabelAt(value, SEG_DEFAULT, label) && label[0])
                e["label"] = label;
            char mod[MAX_MODULE_SIZE] = "";
            if (DbgGetModuleAt(value, mod) && mod[0])
                e["module"] = mod;

            STACK_COMMENT sc{};
            if (DbgStackCommentGet(addr, &sc) && sc.comment[0])
                e["comment"] = sc.comment;

            entries.push_back(e);
        }

        json r;
        r["csp"] = HexAddr(csp);
        r["entries"] = entries;
        return r;
    }

    json MethodAddressInfo(const json& p)
    {
        duint addr = 0;
        if (!ResolveValue(p, "addr", addr))
            throw std::runtime_error("invalid 'addr'");

        json r;
        r["addr"] = HexAddr(addr);

        char mod[MAX_MODULE_SIZE] = "";
        if (DbgGetModuleAt(addr, mod) && mod[0])
            r["module"] = mod;

        char section[64] = "";
        if (DbgFunctions()->SectionFromAddr(addr, section) && section[0])
            r["section"] = section;

        char label[MAX_LABEL_SIZE] = "";
        if (DbgGetLabelAt(addr, SEG_DEFAULT, label) && label[0])
            r["label"] = label;

        char comment[MAX_COMMENT_SIZE] = "";
        if (DbgGetCommentAt(addr, comment) && comment[0])
            r["comment"] = comment;

        char rights[RIGHTS_STRING_SIZE + 16] = "";
        if (DbgFunctions()->GetPageRights(addr, rights) && rights[0])
            r["rights"] = rights;

        duint start = 0, end = 0;
        if (DbgFunctionGet(addr, &start, &end))
        {
            json fn;
            fn["start"] = HexAddr(start);
            fn["end"] = HexAddr(end);
            r["function"] = fn;
        }

        BASIC_INSTRUCTION_INFO info{};
        DbgDisasmFastAt(addr, &info);
        if (info.size)
        {
            r["instruction"] = info.instruction;
            r["instructionSize"] = info.size;
        }

        char str[MAX_STRING_SIZE] = "";
        if (DbgGetStringAt(addr, str) && str[0])
            r["string"] = str;

        return r;
    }

    json MethodReadString(const json& p)
    {
        duint addr = 0;
        if (!ResolveValue(p, "addr", addr))
            throw std::runtime_error("invalid 'addr'");
        char str[MAX_STRING_SIZE] = "";
        bool ok = DbgGetStringAt(addr, str);
        json r;
        r["addr"] = HexAddr(addr);
        r["found"] = ok && str[0] != 0;
        if (ok)
            r["string"] = str;
        return r;
    }

    json MethodAssemble(const json& p)
    {
        if (!DbgIsDebugging())
            throw std::runtime_error("not debugging");
        duint addr = 0;
        if (!ResolveValue(p, "addr", addr))
            throw std::runtime_error("invalid 'addr'");
        if (!p.contains("instruction") || !p["instruction"].is_string())
            throw std::runtime_error("missing 'instruction'");
        std::string instr = p["instruction"].get<std::string>();
        bool fillnop = p.value("fillnop", false);

        char error[MAX_ERROR_SIZE] = "";
        bool ok = DbgFunctions()->AssembleAtEx(addr, instr.c_str(), error, fillnop);
        json r;
        r["assembled"] = ok;
        r["addr"] = HexAddr(addr);
        if (!ok)
            r["error"] = error[0] ? error : "assemble failed";
        return r;
    }

    json MethodSetPageRights(const json& p)
    {
        duint addr = 0;
        if (!ResolveValue(p, "addr", addr))
            throw std::runtime_error("invalid 'addr'");
        if (!p.contains("rights") || !p["rights"].is_string())
            throw std::runtime_error("missing 'rights' (e.g. 'ERWC', 'Full', 'Read')");
        std::string rights = p["rights"].get<std::string>();
        bool ok = DbgFunctions()->SetPageRights(addr, rights.c_str());
        json r;
        r["ok"] = ok;
        return r;
    }

    json MethodIsValidPtr(const json& p)
    {
        duint addr = 0;
        if (!ResolveValue(p, "addr", addr))
            throw std::runtime_error("invalid 'addr'");
        json r;
        r["addr"] = HexAddr(addr);
        r["valid"] = DbgMemIsValidReadPtr(addr);
        return r;
    }

    json MethodMemBase(const json& p)
    {
        duint addr = 0;
        if (!ResolveValue(p, "addr", addr))
            throw std::runtime_error("invalid 'addr'");
        duint size = 0;
        duint base = DbgMemFindBaseAddr(addr, &size);
        json r;
        r["addr"] = HexAddr(addr);
        r["base"] = HexAddr(base);
        r["size"] = (uint64_t)size;
        r["found"] = base != 0;
        return r;
    }

    json MethodBranchDestination(const json& p)
    {
        duint addr = 0;
        if (!ResolveValue(p, "addr", addr))
            throw std::runtime_error("invalid 'addr'");
        duint dest = DbgGetBranchDestination(addr);
        json r;
        r["addr"] = HexAddr(addr);
        r["destination"] = HexAddr(dest);
        return r;
    }

    json MethodXrefs(const json& p)
    {
        duint addr = 0;
        if (!ResolveValue(p, "addr", addr))
            throw std::runtime_error("invalid 'addr'");

        json refs = json::array();
        XREF_INFO info{};
        if (DbgXrefGet(addr, &info) && info.references)
        {
            static const char* kinds[] = { "none", "data", "jmp", "call" };
            for (duint i = 0; i < info.refcount; i++)
            {
                json e;
                e["addr"] = HexAddr(info.references[i].addr);
                int t = (int)info.references[i].type;
                e["type"] = (t >= 0 && t < 4) ? kinds[t] : "unknown";
                refs.push_back(e);
            }
            BridgeFree(info.references);
        }
        json r;
        r["addr"] = HexAddr(addr);
        r["count"] = (uint64_t)DbgGetXrefCountAt(addr);
        r["references"] = refs;
        return r;
    }

    json MethodFindPattern(const json& p)
    {
        duint start = 0;
        if (!ResolveValue(p, "start", start))
            throw std::runtime_error("invalid 'start'");
        duint size = 0;
        if (!ResolveValue(p, "size", size))
            throw std::runtime_error("invalid 'size'");
        if (!p.contains("pattern") || !p["pattern"].is_string())
            throw std::runtime_error("missing 'pattern' (e.g. '48 8B ?? E8')");
        std::string pattern = p["pattern"].get<std::string>();

        duint found = Script::Pattern::FindMem(start, size, pattern.c_str());
        json r;
        r["found"] = found != 0;
        if (found)
            r["addr"] = HexAddr(found);
        return r;
    }

    json MethodLabels(const json&)
    {
        if (!DbgIsDebugging())
            throw std::runtime_error("not debugging");
        ListInfo li{};
        if (!Script::Label::GetList(&li))
            throw std::runtime_error("Label::GetList failed");

        auto* labels = (Script::Label::LabelInfo*)li.data;
        json arr = json::array();
        for (int i = 0; i < li.count; i++)
        {
            json e;
            e["module"] = labels[i].mod;
            e["rva"] = HexAddr(labels[i].rva);
            e["text"] = labels[i].text;
            e["manual"] = labels[i].manual;
            arr.push_back(e);
        }
        if (li.data)
            BridgeFree(li.data);
        json r;
        r["labels"] = arr;
        return r;
    }

    struct SymCollect
    {
        json* arr;
        std::string filter;
        size_t max;
    };

    bool SymEnumCb(const SYMBOLPTR_* symbol, void* user)
    {
        auto* c = (SymCollect*)user;
        if (c->arr->size() >= c->max)
            return false; // stop enumeration
        SYMBOLINFO info{};
        DbgGetSymbolInfo((const SYMBOLPTR*)symbol, &info);
        const char* name = info.undecoratedSymbol ? info.undecoratedSymbol
            : (info.decoratedSymbol ? info.decoratedSymbol : "");
        std::string n = name;
        if (c->filter.empty() || n.find(c->filter) != std::string::npos)
        {
            json e;
            e["addr"] = HexAddr(info.addr);
            e["name"] = n;
            e["type"] = (int)info.type;
            if (info.ordinal)
                e["ordinal"] = (uint32_t)info.ordinal;
            c->arr->push_back(e);
        }
        if (info.freeDecorated && info.decoratedSymbol)
            BridgeFree(info.decoratedSymbol);
        if (info.freeUndecorated && info.undecoratedSymbol)
            BridgeFree(info.undecoratedSymbol);
        return true;
    }

    json MethodSymbols(const json& p)
    {
        if (!DbgIsDebugging())
            throw std::runtime_error("not debugging");
        if (!p.contains("module") || !p["module"].is_string())
            throw std::runtime_error("missing 'module'");
        std::string mod = p["module"].get<std::string>();
        duint base = DbgFunctions()->ModBaseFromName(mod.c_str());
        if (!base)
            throw std::runtime_error("module not found: " + mod);

        json arr = json::array();
        SymCollect c{ &arr, p.value("filter", std::string()), p.value("max", (size_t)500) };
        DbgSymbolEnum(base, SymEnumCb, &c);

        json r;
        r["module"] = mod;
        r["symbols"] = arr;
        r["truncated"] = (arr.size() >= c.max);
        return r;
    }

    json MethodHandles(const json&)
    {
        if (!DbgIsDebugging())
            throw std::runtime_error("not debugging");
        ListInfo li{};
        if (!DbgFunctions()->EnumHandles(&li))
            throw std::runtime_error("EnumHandles failed");

        auto* handles = (HANDLEINFO*)li.data;
        json arr = json::array();
        for (int i = 0; i < li.count; i++)
        {
            json e;
            e["handle"] = HexAddr(handles[i].Handle);
            e["typeNumber"] = (int)handles[i].TypeNumber;
            e["grantedAccess"] = (uint32_t)handles[i].GrantedAccess;

            char name[1024] = "", typeName[256] = "";
            if (DbgFunctions()->GetHandleName(handles[i].Handle, name, sizeof(name), typeName, sizeof(typeName)))
            {
                if (typeName[0]) e["type"] = typeName;
                if (name[0]) e["name"] = name;
            }
            arr.push_back(e);
        }
        if (li.data)
            BridgeFree(li.data);
        json r;
        r["handles"] = arr;
        return r;
    }

    json MethodTcpConnections(const json&)
    {
        if (!DbgIsDebugging())
            throw std::runtime_error("not debugging");
        ListInfo li{};
        if (!DbgFunctions()->EnumTcpConnections(&li))
            throw std::runtime_error("EnumTcpConnections failed");

        auto* conns = (TCPCONNECTIONINFO*)li.data;
        json arr = json::array();
        for (int i = 0; i < li.count; i++)
        {
            json e;
            e["local"] = std::string(conns[i].LocalAddress) + ":" + std::to_string(conns[i].LocalPort);
            e["remote"] = std::string(conns[i].RemoteAddress) + ":" + std::to_string(conns[i].RemotePort);
            e["state"] = conns[i].StateText;
            arr.push_back(e);
        }
        if (li.data)
            BridgeFree(li.data);
        json r;
        r["connections"] = arr;
        return r;
    }

    json MethodPatches(const json&)
    {
        if (!DbgIsDebugging())
            throw std::runtime_error("not debugging");
        size_t cbsize = 0;
        // First call to get required size.
        DbgFunctions()->PatchEnum(nullptr, &cbsize);
        json arr = json::array();
        if (cbsize)
        {
            std::vector<uint8_t> buf(cbsize);
            auto* patches = (DBGPATCHINFO*)buf.data();
            if (DbgFunctions()->PatchEnum(patches, nullptr))
            {
                size_t count = cbsize / sizeof(DBGPATCHINFO);
                for (size_t i = 0; i < count; i++)
                {
                    json e;
                    e["addr"] = HexAddr(patches[i].addr);
                    e["module"] = patches[i].mod;
                    e["oldByte"] = (int)patches[i].oldbyte;
                    e["newByte"] = (int)patches[i].newbyte;
                    arr.push_back(e);
                }
            }
        }
        json r;
        r["patches"] = arr;
        return r;
    }

    json MethodMemAlloc(const json& p)
    {
        if (!DbgIsDebugging())
            throw std::runtime_error("not debugging");
        duint size = 0;
        if (!ResolveValue(p, "size", size))
            throw std::runtime_error("invalid 'size'");
        char cmd[64];
        sprintf_s(cmd, sizeof(cmd), "alloc 0x%llX", (unsigned long long)size);
        if (!DbgCmdExecDirect(cmd))
            throw std::runtime_error("alloc command failed");
        bool ok = false;
        duint addr = DbgEval("$result", &ok);
        json r;
        r["allocated"] = ok && addr != 0;
        if (ok)
            r["addr"] = HexAddr(addr);
        r["size"] = (uint64_t)size;
        return r;
    }

    json MethodMemFree(const json& p)
    {
        if (!DbgIsDebugging())
            throw std::runtime_error("not debugging");
        duint addr = 0;
        if (!ResolveValue(p, "addr", addr))
            throw std::runtime_error("invalid 'addr'");
        char cmd[64];
        sprintf_s(cmd, sizeof(cmd), "free 0x%llX", (unsigned long long)addr);
        bool ok = DbgCmdExecDirect(cmd);
        json r;
        r["freed"] = ok;
        return r;
    }

    json Dispatch(const std::string& method, const json& params)
    {
        if (method == "ping")          return MethodPing(params);
        if (method == "status")        return MethodStatus(params);
        if (method == "exec")          return MethodExec(params);
        if (method == "eval")          return MethodEval(params);
        if (method == "read_memory")   return MethodReadMemory(params);
        if (method == "write_memory")  return MethodWriteMemory(params);
        if (method == "registers")     return MethodRegisters(params);
        if (method == "disasm")        return MethodDisasm(params);
        if (method == "memmap")        return MethodMemMap(params);
        if (method == "bp_list")       return MethodBpList(params);
        if (method == "process_list")  return MethodProcessList(params);
        if (method == "threads")       return MethodThreads(params);
        if (method == "modules")       return MethodModules(params);
        if (method == "module_sections") return MethodModuleSections(params);
        if (method == "module_exports") return MethodModuleExports(params);
        if (method == "call_stack")    return MethodCallStack(params);
        if (method == "read_stack")    return MethodReadStack(params);
        if (method == "address_info")  return MethodAddressInfo(params);
        if (method == "read_string")   return MethodReadString(params);
        if (method == "assemble")      return MethodAssemble(params);
        if (method == "set_page_rights") return MethodSetPageRights(params);
        if (method == "is_valid_ptr")  return MethodIsValidPtr(params);
        if (method == "mem_base")      return MethodMemBase(params);
        if (method == "branch_destination") return MethodBranchDestination(params);
        if (method == "xrefs")         return MethodXrefs(params);
        if (method == "find_pattern")  return MethodFindPattern(params);
        if (method == "labels")        return MethodLabels(params);
        if (method == "symbols")       return MethodSymbols(params);
        if (method == "handles")       return MethodHandles(params);
        if (method == "tcp_connections") return MethodTcpConnections(params);
        if (method == "patches")       return MethodPatches(params);
        if (method == "mem_alloc")     return MethodMemAlloc(params);
        if (method == "mem_free")      return MethodMemFree(params);
        throw std::runtime_error("unknown method: " + method);
    }
}

std::string HandleRpc(const std::string& body)
{
    json response;
    try
    {
        json req = json::parse(body);
        std::string method = req.at("method").get<std::string>();
        json params = req.contains("params") ? req["params"] : json::object();
        if (!params.is_object())
            params = json::object();

        response["ok"] = true;
        response["result"] = Dispatch(method, params);
    }
    catch (const std::exception& e)
    {
        response = json::object();
        response["ok"] = false;
        response["error"] = e.what();
    }
    return response.dump();
}
