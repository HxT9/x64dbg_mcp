using System.ComponentModel;
using System.Text.Json;
using ModelContextProtocol.Server;

namespace X64DbgMcp;

/// <summary>
/// MCP tools exposing x64dbg to an LLM. Each tool forwards to the x64dbg_mcp
/// plugin over its local HTTP endpoint. Most actions map onto x64dbg commands;
/// reads (memory, registers, disasm, memory map, breakpoints) return structured JSON.
/// </summary>
[McpServerToolType]
public static class X64DbgTools
{
    [McpServerTool(Name = "dbg_status")]
    [Description("Full debugger status/info: debugging? running vs paused; the target (name/path/base/entry), pid, peb, command line, current thread id and thread count, and cip/module/label/instruction. IMPORTANT: when running=true the CPU state (registers/cip/stack) is a stale snapshot — 'stateLive' is false and you must pause for coherent state.")]
    public static Task<string> Status(X64DbgClient client)
        => client.CallJsonAsync("status");

    [McpServerTool(Name = "dbg_command")]
    [Description("Execute a raw x64dbg command synchronously (e.g. 'init C:\\\\path\\\\app.exe', 'run', 'bp kernel32.CreateFileW', 'StepOver'). Returns whether the command was accepted. Use this for anything without a dedicated tool.")]
    public static Task<string> Command(
        X64DbgClient client,
        [Description("The x64dbg command line to execute.")] string command)
        => client.CallJsonAsync("exec", new { cmd = command });

    [McpServerTool(Name = "dbg_eval")]
    [Description("Evaluate an x64dbg expression and return its numeric value (decimal + hex). Accepts registers, module exports, arithmetic, etc. (e.g. 'cip', 'kernel32.CreateFileW', 'rax+8', '[rsp]').")]
    public static Task<string> Eval(
        X64DbgClient client,
        [Description("The expression to evaluate.")] string expression)
        => client.CallJsonAsync("eval", new { expr = expression });

    [McpServerTool(Name = "dbg_read_memory")]
    [Description("Read bytes from the debuggee's memory. 'address' may be a number or any x64dbg expression. Returns the bytes as a hex string.")]
    public static Task<string> ReadMemory(
        X64DbgClient client,
        [Description("Address or expression to read from (e.g. 'rip', '0x7ff6...', 'app.00401000').")] string address,
        [Description("Number of bytes to read (1..16777216).")] int size)
        => client.CallJsonAsync("read_memory", new { addr = address, size });

    [McpServerTool(Name = "dbg_write_memory")]
    [Description("Write bytes into the debuggee's memory. 'address' may be a number or expression. 'hexBytes' is a hex string like 'C3' or '4889e5'.")]
    public static Task<string> WriteMemory(
        X64DbgClient client,
        [Description("Destination address or expression.")] string address,
        [Description("Bytes to write, as a hex string (no spaces, even length).")] string hexBytes)
        => client.CallJsonAsync("write_memory", new { addr = address, data = hexBytes });

    [McpServerTool(Name = "dbg_registers")]
    [Description("Read the general-purpose CPU registers of the current thread (architecture-neutral names: cax, cbx, ... cip; r8-r15 on x64; eflags).")]
    public static Task<string> Registers(X64DbgClient client)
        => client.CallJsonAsync("registers");

    [McpServerTool(Name = "dbg_disassemble")]
    [Description("Disassemble 'count' instructions starting at 'address'. Returns each instruction's address, size, text, and branch target when applicable.")]
    public static Task<string> Disassemble(
        X64DbgClient client,
        [Description("Address or expression to start from (e.g. 'cip').")] string address,
        [Description("Number of instructions to disassemble (1..2048).")] int count = 10)
        => client.CallJsonAsync("disasm", new { addr = address, count });

    [McpServerTool(Name = "dbg_memory_map")]
    [Description("Get the debuggee's memory map: base, size, protection, state, type, and info (module/section) of each region.")]
    public static Task<string> MemoryMap(X64DbgClient client)
        => client.CallJsonAsync("memmap");

    [McpServerTool(Name = "dbg_list_breakpoints")]
    [Description("List all breakpoints (software, hardware, memory, dll, exception) with their address, state, type, and hit count.")]
    public static Task<string> ListBreakpoints(X64DbgClient client)
        => client.CallJsonAsync("bp_list");

    [McpServerTool(Name = "dbg_set_breakpoint")]
    [Description("Set a breakpoint at an address/expression. type: 'software' (default), 'hardware', or 'memory'.")]
    public static Task<string> SetBreakpoint(
        X64DbgClient client,
        [Description("Address or expression for the breakpoint.")] string address,
        [Description("Breakpoint type: software | hardware | memory.")] string type = "software")
    {
        var cmd = type.ToLowerInvariant() switch
        {
            "hardware" or "hw" => $"bph {address}",
            "memory" or "mem" => $"bpm {address}",
            _ => $"bp {address}",
        };
        return client.CallJsonAsync("exec", new { cmd });
    }

    [McpServerTool(Name = "dbg_delete_breakpoint")]
    [Description("Delete the software breakpoint at the given address/expression.")]
    public static Task<string> DeleteBreakpoint(
        X64DbgClient client,
        [Description("Address or expression of the breakpoint to remove.")] string address)
        => client.CallJsonAsync("exec", new { cmd = $"bc {address}" });

    [McpServerTool(Name = "dbg_run")]
    [Description("Resume execution of the debuggee (equivalent to F9 / the 'run' command).")]
    public static Task<string> Run(X64DbgClient client)
        => client.CallJsonAsync("exec", new { cmd = "run" });

    [McpServerTool(Name = "dbg_pause")]
    [Description("Pause the running debuggee.")]
    public static Task<string> Pause(X64DbgClient client)
        => client.CallJsonAsync("exec", new { cmd = "pause" });

    [McpServerTool(Name = "dbg_step_into")]
    [Description("Step into one instruction (single-step). Use dbg_step_over to step over calls.")]
    public static Task<string> StepInto(X64DbgClient client)
        => client.CallJsonAsync("exec", new { cmd = "sti" });

    [McpServerTool(Name = "dbg_step_over")]
    [Description("Step over one instruction, executing calls to completion.")]
    public static Task<string> StepOver(X64DbgClient client)
        => client.CallJsonAsync("exec", new { cmd = "sto" });

    [McpServerTool(Name = "dbg_step_out")]
    [Description("Run until the current function returns (step out).")]
    public static Task<string> StepOut(X64DbgClient client)
        => client.CallJsonAsync("exec", new { cmd = "rtr" });

    // ----- session / process control -----------------------------------------

    [McpServerTool(Name = "dbg_process_list")]
    [Description("List running processes on the system (pid, exe, window title, args) — use to pick a target for dbg_attach. Works even when not debugging.")]
    public static Task<string> ProcessList(X64DbgClient client)
        => client.CallJsonAsync("process_list");

    [McpServerTool(Name = "dbg_attach")]
    [Description("Attach the debugger to a running process by PID. Guarded: if already attached to that same PID it is a no-op; if attached to a different target it refuses (detach/stop first) — re-attaching to the active debuggee would kill it.")]
    public static async Task<string> Attach(
        X64DbgClient client,
        [Description("Process id (decimal) to attach to.")] int pid)
    {
        // Re-issuing 'attach' while already debugging the target terminates the
        // process, so check the current session first.
        var status = await client.CallAsync("status");
        if (status.TryGetProperty("debugging", out var dbg) && dbg.GetBoolean())
        {
            int curPid = status.TryGetProperty("pid", out var p) ? p.GetInt32() : 0;
            if (curPid == pid)
                return $"{{\"ok\":true,\"noop\":true,\"message\":\"already attached to PID {pid}\"}}";
            return $"{{\"ok\":false,\"error\":\"already debugging PID {curPid}; detach (dbg_detach) or stop (dbg_stop) before attaching to PID {pid}\"}}";
        }
        return await client.CallJsonAsync("exec", new { cmd = $"attach {pid:X}" });
    }

    [McpServerTool(Name = "dbg_detach")]
    [Description("Detach from the current debuggee, leaving it running.")]
    public static Task<string> Detach(X64DbgClient client)
        => client.CallJsonAsync("exec", new { cmd = "detach" });

    [McpServerTool(Name = "dbg_load")]
    [Description("Load and start debugging an executable. Optionally pass command-line arguments.")]
    public static Task<string> Load(
        X64DbgClient client,
        [Description("Full path to the executable to debug.")] string path,
        [Description("Optional command-line arguments for the target.")] string? args = null)
    {
        var cmd = string.IsNullOrWhiteSpace(args) ? $"init \"{path}\"" : $"init \"{path}\", \"{args}\"";
        return client.CallJsonAsync("exec", new { cmd });
    }

    [McpServerTool(Name = "dbg_stop")]
    [Description("Stop debugging / terminate the current debuggee.")]
    public static Task<string> Stop(X64DbgClient client)
        => client.CallJsonAsync("exec", new { cmd = "stop" });

    [McpServerTool(Name = "dbg_restart")]
    [Description("Restart the current debuggee from the beginning.")]
    public static Task<string> Restart(X64DbgClient client)
        => client.CallJsonAsync("exec", new { cmd = "restart" });

    // ----- threads / modules / stack ------------------------------------------

    [McpServerTool(Name = "dbg_threads")]
    [Description("List the debuggee's threads (number, id, cip, entry, suspend count, name) and which is current.")]
    public static Task<string> Threads(X64DbgClient client)
        => client.CallJsonAsync("threads");

    [McpServerTool(Name = "dbg_switch_thread")]
    [Description("Switch the active thread by thread id.")]
    public static Task<string> SwitchThread(
        X64DbgClient client,
        [Description("Thread id to switch to.")] int threadId)
        => client.CallJsonAsync("exec", new { cmd = $"switchthread {threadId:X}" });

    [McpServerTool(Name = "dbg_modules")]
    [Description("List loaded modules (base, size, entry, section count, name, path).")]
    public static Task<string> Modules(X64DbgClient client)
        => client.CallJsonAsync("modules");

    [McpServerTool(Name = "dbg_module_sections")]
    [Description("List the PE sections of a module (address, size, name).")]
    public static Task<string> ModuleSections(
        X64DbgClient client,
        [Description("Module name (e.g. 'kernel32.dll' or 'app.exe').")] string module)
        => client.CallJsonAsync("module_sections", new { module });

    [McpServerTool(Name = "dbg_module_exports")]
    [Description("List a module's exported functions (ordinal, va, rva, name). Optionally filter by substring; capped by 'max'.")]
    public static Task<string> ModuleExports(
        X64DbgClient client,
        [Description("Module name (e.g. 'kernel32.dll').")] string module,
        [Description("Optional case-sensitive substring filter on the export name.")] string? filter = null,
        [Description("Maximum number of exports to return (default 200).")] int max = 200)
        => client.CallJsonAsync("module_exports", new { module, filter = filter ?? "", max });

    [McpServerTool(Name = "dbg_call_stack")]
    [Description("Get the current call stack / backtrace (return address slot, caller 'from', callee 'to', comment).")]
    public static Task<string> CallStack(X64DbgClient client)
        => client.CallJsonAsync("call_stack");

    [McpServerTool(Name = "dbg_read_stack")]
    [Description("Read the top of the stack: 'count' pointer-sized slots from csp, each resolved to a label/module/comment when possible.")]
    public static Task<string> ReadStack(
        X64DbgClient client,
        [Description("Number of stack slots to read (1..256, default 16).")] int count = 16)
        => client.CallJsonAsync("read_stack", new { count });

    // ----- introspection / patching -------------------------------------------

    [McpServerTool(Name = "dbg_address_info")]
    [Description("Describe an address: module, section, label, comment, page rights, enclosing function bounds, the instruction at it, and any string.")]
    public static Task<string> AddressInfo(
        X64DbgClient client,
        [Description("Address or expression to describe.")] string address)
        => client.CallJsonAsync("address_info", new { addr = address });

    [McpServerTool(Name = "dbg_read_string")]
    [Description("Read a string at an address, auto-detecting ASCII/UTF-16 the way x64dbg displays it.")]
    public static Task<string> ReadString(
        X64DbgClient client,
        [Description("Address or expression of the string.")] string address)
        => client.CallJsonAsync("read_string", new { addr = address });

    [McpServerTool(Name = "dbg_set_register")]
    [Description("Set a register to a value (e.g. name='cip', value='app.00401000'). Accepts any x64dbg expression as the value.")]
    public static Task<string> SetRegister(
        X64DbgClient client,
        [Description("Register name (cax, cbx, ... cip, r8-r15, eflags, zf, cf, ...).")] string name,
        [Description("New value or expression.")] string value)
        => client.CallJsonAsync("exec", new { cmd = $"{name}={value}" });

    [McpServerTool(Name = "dbg_assemble")]
    [Description("Assemble an instruction and write it at an address (in-memory patch). Set fillnop=true to pad leftover bytes with NOPs.")]
    public static Task<string> Assemble(
        X64DbgClient client,
        [Description("Address or expression to assemble at.")] string address,
        [Description("Assembly instruction, e.g. 'jmp 0x401000' or 'nop'.")] string instruction,
        [Description("Pad remaining bytes of the replaced instruction with NOPs.")] bool fillnop = false)
        => client.CallJsonAsync("assemble", new { addr = address, instruction, fillnop });

    [McpServerTool(Name = "dbg_set_comment")]
    [Description("Set (or clear) the comment at an address.")]
    public static Task<string> SetComment(
        X64DbgClient client,
        [Description("Address or expression.")] string address,
        [Description("Comment text (empty to clear).")] string text)
        => client.CallJsonAsync("exec", new { cmd = $"cmt {address}, \"{text}\"" });

    [McpServerTool(Name = "dbg_set_label")]
    [Description("Set (or clear) the label at an address.")]
    public static Task<string> SetLabel(
        X64DbgClient client,
        [Description("Address or expression.")] string address,
        [Description("Label text (empty to clear).")] string text)
        => client.CallJsonAsync("exec", new { cmd = $"lbl {address}, \"{text}\"" });

    [McpServerTool(Name = "dbg_set_page_rights")]
    [Description("Change memory protection of the page containing an address. rights e.g. 'Full', 'Read', 'ERWC', 'ER'.")]
    public static Task<string> SetPageRights(
        X64DbgClient client,
        [Description("Address or expression within the target page.")] string address,
        [Description("New protection string (Full | Read | ReadWrite | Execute | ERWC | ...).")] string rights)
        => client.CallJsonAsync("set_page_rights", new { addr = address, rights });

    // ----- analysis / search --------------------------------------------------

    [McpServerTool(Name = "dbg_is_valid_ptr")]
    [Description("Check whether an address points to readable memory in the debuggee.")]
    public static Task<string> IsValidPtr(
        X64DbgClient client,
        [Description("Address or expression to test.")] string address)
        => client.CallJsonAsync("is_valid_ptr", new { addr = address });

    [McpServerTool(Name = "dbg_memory_base")]
    [Description("Find the base address and size of the memory region containing an address.")]
    public static Task<string> MemoryBase(
        X64DbgClient client,
        [Description("Address or expression within the region.")] string address)
        => client.CallJsonAsync("mem_base", new { addr = address });

    [McpServerTool(Name = "dbg_branch_destination")]
    [Description("Resolve the branch/call/jump target of the instruction at an address (0 if not a branch).")]
    public static Task<string> BranchDestination(
        X64DbgClient client,
        [Description("Address or expression of the instruction.")] string address)
        => client.CallJsonAsync("branch_destination", new { addr = address });

    [McpServerTool(Name = "dbg_xrefs")]
    [Description("Get cross-references (data/jmp/call) to an address, with a total count.")]
    public static Task<string> Xrefs(
        X64DbgClient client,
        [Description("Target address or expression.")] string address)
        => client.CallJsonAsync("xrefs", new { addr = address });

    [McpServerTool(Name = "dbg_find_pattern")]
    [Description("Search a memory range for a byte pattern (x64dbg syntax, wildcards allowed, e.g. '48 8B ?? E8'). Returns the first match address.")]
    public static Task<string> FindPattern(
        X64DbgClient client,
        [Description("Start address or expression of the search range.")] string start,
        [Description("Size of the range to search (number or expression).")] string size,
        [Description("Byte pattern, e.g. '48 8B ?? E8' (?? = wildcard).")] string pattern)
        => client.CallJsonAsync("find_pattern", new { start, size, pattern });

    [McpServerTool(Name = "dbg_labels")]
    [Description("List all user/auto labels (module, rva, text).")]
    public static Task<string> Labels(X64DbgClient client)
        => client.CallJsonAsync("labels");

    [McpServerTool(Name = "dbg_symbols")]
    [Description("Enumerate the symbols of a module (addr, name, type, ordinal). Optionally filter by substring; capped by 'max'.")]
    public static Task<string> Symbols(
        X64DbgClient client,
        [Description("Module name (e.g. 'kernel32.dll').")] string module,
        [Description("Optional case-sensitive substring filter on the symbol name.")] string? filter = null,
        [Description("Maximum number of symbols to return (default 500).")] int max = 500)
        => client.CallJsonAsync("symbols", new { module, filter = filter ?? "", max });

    [McpServerTool(Name = "dbg_handles")]
    [Description("Enumerate the debuggee's open handles (handle, type, name, granted access).")]
    public static Task<string> Handles(X64DbgClient client)
        => client.CallJsonAsync("handles");

    [McpServerTool(Name = "dbg_tcp_connections")]
    [Description("List the debuggee's TCP connections (local/remote endpoints and state).")]
    public static Task<string> TcpConnections(X64DbgClient client)
        => client.CallJsonAsync("tcp_connections");

    [McpServerTool(Name = "dbg_patches")]
    [Description("List all in-memory patches applied to the debuggee (address, module, old/new byte).")]
    public static Task<string> Patches(X64DbgClient client)
        => client.CallJsonAsync("patches");

    [McpServerTool(Name = "dbg_mem_alloc")]
    [Description("Allocate memory inside the debuggee. Returns the allocated base address.")]
    public static Task<string> MemAlloc(
        X64DbgClient client,
        [Description("Number of bytes to allocate (number or expression).")] string size)
        => client.CallJsonAsync("mem_alloc", new { size });

    [McpServerTool(Name = "dbg_mem_free")]
    [Description("Free memory previously allocated in the debuggee with dbg_mem_alloc.")]
    public static Task<string> MemFree(
        X64DbgClient client,
        [Description("Base address to free.")] string address)
        => client.CallJsonAsync("mem_free", new { addr = address });

    // ----- flags / stack helpers / convenience --------------------------------

    [McpServerTool(Name = "dbg_flag_get")]
    [Description("Read a CPU flag (zf, cf, sf, of, pf, af, df, tf, if) — returns its value (0/1).")]
    public static Task<string> FlagGet(
        X64DbgClient client,
        [Description("Flag name: zf | cf | sf | of | pf | af | df | tf | if.")] string flag)
        => client.CallJsonAsync("eval", new { expr = flag });

    [McpServerTool(Name = "dbg_flag_set")]
    [Description("Set a CPU flag to 0 or 1 (zf, cf, sf, of, pf, af, df, tf, if).")]
    public static Task<string> FlagSet(
        X64DbgClient client,
        [Description("Flag name: zf | cf | sf | of | pf | af | df | tf | if.")] string flag,
        [Description("New value: true (1) or false (0).")] bool value)
        => client.CallJsonAsync("exec", new { cmd = $"{flag}={(value ? 1 : 0)}" });

    [McpServerTool(Name = "dbg_stack_push")]
    [Description("Push a value (number or expression) onto the debuggee's stack.")]
    public static Task<string> StackPush(
        X64DbgClient client,
        [Description("Value or expression to push.")] string value)
        => client.CallJsonAsync("exec", new { cmd = $"push {value}" });

    [McpServerTool(Name = "dbg_stack_pop")]
    [Description("Pop the top value off the debuggee's stack (csp += pointer size).")]
    public static Task<string> StackPop(X64DbgClient client)
        => client.CallJsonAsync("exec", new { cmd = "pop" });

    [McpServerTool(Name = "dbg_step_into_disasm")]
    [Description("Step into one instruction, then disassemble from the new cip — convenient for tracing.")]
    public static async Task<string> StepIntoDisasm(
        X64DbgClient client,
        [Description("How many instructions to disassemble at the new cip (default 3).")] int count = 3)
    {
        await client.CallJsonAsync("exec", new { cmd = "sti" });
        return await client.CallJsonAsync("disasm", new { addr = "cip", count });
    }
}
