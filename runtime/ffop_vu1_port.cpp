#include "ffop_vu1_port.h"

#include "runtime/native/native_gif.h"
#include "runtime/native/native_vu1_sandbox.h"
#include "runtime/ps2_vu1_lifted.h"
#include "runtime/ps2_vu1_lifted_ops.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#if FFOP_VU1_LIFT
namespace ps2x::vu1lift
{
    template <bool kSticky>
    uint64_t ffop382e(uint32_t entry, VU1State &state, uint8_t *data, uint32_t top, Vu1Kick out, void *user);
    template <bool kSticky>
    uint64_t ffop12e7(uint32_t entry, VU1State &state, uint8_t *data, uint32_t top, Vu1Kick out, void *user);
    template <bool kSticky>
    uint64_t ffop81b6(uint32_t entry, VU1State &state, uint8_t *data, uint32_t top, Vu1Kick out, void *user);
    template <bool kSticky>
    uint64_t ffopea3c(uint32_t entry, VU1State &state, uint8_t *data, uint32_t top, Vu1Kick out, void *user);
}
#endif

namespace ffop
{
    namespace
    {
        using ps2x::native::Vu1StreamAdapter;
        using ps2x::vu1lift::Vu1Kick;
        using Lifted = uint64_t (*)(uint32_t, VU1State &, uint8_t *, uint32_t, Vu1Kick, void *);

        constexpr uint32_t kVuMemory = 16u * 1024u;

        struct Program
        {
            const char *name;
            uint64_t hash;
            uint32_t pairs;
            std::vector<uint32_t> entries; // recomp/vu1_lift_spec.json's
            Lifted sticky, plain;
        };

#if FFOP_VU1_LIFT
        const Program kPrograms[] = {
            {"382e53ba", 0x382e53ba1c590497ull, 2048u, {0x10u, 0x148u, 0x1418u}, ps2x::vu1lift::ffop382e<true>,
             ps2x::vu1lift::ffop382e<false>},
            {"12e72aa9", 0x12e72aa9aa1230a3ull, 580u, {0x0u, 0x1b8u, 0x1208u}, ps2x::vu1lift::ffop12e7<true>,
             ps2x::vu1lift::ffop12e7<false>},
            {"81b61065", 0x81b6106525d1ecacull, 528u, {0x0u, 0x200u, 0x2d0u, 0x610u, 0x910u, 0xc78u, 0x1068u},
             ps2x::vu1lift::ffop81b6<true>, ps2x::vu1lift::ffop81b6<false>},
            {"ea3c12da", 0xea3c12da55fe0a86ull, 622u, {0x0u, 0x250u, 0x1360u}, ps2x::vu1lift::ffopea3c<true>,
             ps2x::vu1lift::ffopea3c<false>},
        };
#else
        const std::vector<Program> kPrograms;
#endif

        enum class Mode
        {
            Off,
            On,
            Check,
        };

        struct State
        {
            Mode mode = Mode::Off;
            uint64_t codeVersion = ~0ull;
            const Program *program = nullptr; // the code memory's, when lifted
            uint64_t ported = 0, resumed = 0, sandboxed = 0, checked = 0, mismatches = 0, frames = 0;
        } g_state;

        uint64_t codeHash(const uint8_t *code, uint32_t bytes)
        {
            uint64_t h = 0xCBF29CE484222325ull;
            for (uint32_t i = 0; i < bytes; ++i)
                h = (h ^ code[i]) * 0x100000001B3ull;
            return h;
        }

        const Program *identify(const Vu1StreamAdapter &adapter)
        {
            State &s = g_state;
            if (adapter.codeVersion() == s.codeVersion)
                return s.program;
            s.codeVersion = adapter.codeVersion();
            s.program = nullptr;
            for (const Program &program : kPrograms)
                if (codeHash(adapter.code(), program.pairs * 8u) == program.hash)
                    s.program = &program;
            return s.program;
        }

        bool lifted(const Program *program, uint32_t pc)
        {
            return program && std::find(program->entries.begin(), program->entries.end(), pc) != program->entries.end();
        }

        void kickToGif(void *user, const uint8_t *packet, uint32_t bytes)
        {
            static_cast<ps2x::native::GifToNative *>(user)->packet(packet, bytes);
        }

        void kickToList(void *user, const uint8_t *packet, uint32_t bytes)
        {
            static_cast<std::vector<std::vector<uint8_t>> *>(user)->emplace_back(packet, packet + bytes);
        }

        uint64_t runLifted(Vu1StreamAdapter &adapter, const Program &program, uint32_t pc, Vu1Kick kick, void *user)
        {
            VU1Interpreter &vu = adapter.sandbox().vu();
            Vu1Lifted::landQueued(vu);
            VU1State &state = vu.state();
            const ps2x::vu1lift::RoundTowardZero rounding;
            const bool sticky = (state.status & 0x0C0u) != 0x0C0u;
            const uint64_t cycles = (sticky ? program.sticky : program.plain)(pc, state, adapter.dataMemory(), adapter.top(), kick, user);
            state.top = adapter.top();
            state.itop = adapter.itop();
            Vu1Lifted::setClip(vu, state.clip);
            Vu1Lifted::advanceTo(vu, Vu1Lifted::cycle(vu) + cycles);
            state.cycles = Vu1Lifted::cycle(vu);
            return cycles;
        }

        void runSandbox(Vu1StreamAdapter &adapter, uint32_t pc, bool resumed)
        {
            ps2x::native::Vu1Sandbox &sandbox = adapter.sandbox();
            uint8_t *code = const_cast<uint8_t *>(adapter.code());
            const uint64_t cyclesBefore = sandbox.cycles();
            if (resumed)
                sandbox.resume(code, kVuMemory, adapter.dataMemory(), kVuMemory, adapter.top(), adapter.itop());
            else
                sandbox.call(code, kVuMemory, adapter.dataMemory(), kVuMemory, pc, adapter.top(), adapter.itop());
            adapter.addPortCycles(sandbox.cycles() - cyclesBefore);
        }

        std::string difference(const VU1State &port, const VU1State &vu1)
        {
            std::string out;
            char item[96];
            for (int r = 0; r < 32; ++r)
                if (std::memcmp(port.vf[r], vu1.vf[r], sizeof(port.vf[r])) != 0)
                {
                    std::snprintf(item, sizeof(item), " vf%d", r);
                    out += item;
                }
            for (int r = 0; r < 16; ++r)
                if (port.vi[r] != vu1.vi[r])
                {
                    std::snprintf(item, sizeof(item), " vi%d %#x/%#x", r, port.vi[r], vu1.vi[r]);
                    out += item;
                }
            if (std::memcmp(port.acc, vu1.acc, sizeof(port.acc)) != 0)
                out += " acc";
            if (std::memcmp(&port.q, &vu1.q, 4) != 0 || std::memcmp(&port.i, &vu1.i, 4) != 0 || port.r != vu1.r)
                out += " q/i/r";
            if (port.clip != vu1.clip || port.mac != vu1.mac || port.status != vu1.status)
            {
                std::snprintf(item, sizeof(item), " clip %#x/%#x mac %#x/%#x status %#x/%#x", port.clip, vu1.clip, port.mac,
                              vu1.mac, port.status, vu1.status);
                out += item;
            }
            if (port.pc != vu1.pc)
            {
                std::snprintf(item, sizeof(item), " pc %#x/%#x", port.pc, vu1.pc);
                out += item;
            }
            return out;
        }

        // The lifted program, then the sandbox from the same state and memory: the sandbox's results stay.
        void check(Vu1StreamAdapter &adapter, const Program &program, uint32_t pc, bool resumed)
        {
            State &s = g_state;
            ps2x::native::Vu1Sandbox &sandbox = adapter.sandbox();
            VU1Interpreter &vu = sandbox.vu();
            uint8_t *data = adapter.dataMemory();
            static std::vector<uint8_t> s_before(kVuMemory), s_portData(kVuMemory);
            static std::vector<std::vector<uint8_t>> s_portPackets, s_packets;
            s_portPackets.clear();
            s_packets.clear();
            std::memcpy(s_before.data(), data, kVuMemory);
            Vu1Lifted::landQueued(vu);
            const VU1State before = vu.state();
            const uint64_t portCycles = runLifted(adapter, program, pc, kickToList, &s_portPackets);
            const VU1State portState = vu.state();
            std::memcpy(s_portData.data(), data, kVuMemory);
            std::memcpy(data, s_before.data(), kVuMemory);
            const uint64_t clock = vu.state().cycles;
            vu.state() = before;
            vu.state().cycles = clock;
            Vu1Lifted::setClip(vu, before.clip);
            vu.setPacketObserver([&sandbox](const uint8_t *packet, uint32_t bytes)
                                 {
                                     s_packets.emplace_back(packet, packet + bytes);
                                     sandbox.gif().packet(packet, bytes);
                                 });
            const uint64_t cyclesBefore = sandbox.cycles();
            runSandbox(adapter, pc, resumed);
            vu.setPacketObserver([&sandbox](const uint8_t *packet, uint32_t bytes) { sandbox.gif().packet(packet, bytes); });
            const uint64_t cycles = sandbox.cycles() - cyclesBefore;
            ++s.checked;
            std::string differs = difference(portState, vu.state());
            if (s_portPackets != s_packets)
                differs += " packets " + std::to_string(s_portPackets.size()) + "/" + std::to_string(s_packets.size());
            if (std::memcmp(s_portData.data(), data, kVuMemory) != 0)
                differs += " memory";
            if (portCycles != cycles)
                differs += " cycles " + std::to_string(portCycles) + "/" + std::to_string(cycles);
            if (differs.empty())
                return;
            ++s.mismatches;
            static int s_logged = 0;
            if (s_logged++ < 50)
                std::fprintf(stderr, "[ffop:vu1] %s at %#x%s differs from the sandbox (TOP %#x):%s\n", program.name, pc,
                             resumed ? " (MSCNT)" : "", adapter.top(), differs.c_str());
        }

        Vu1StreamAdapter::Choice choose(Vu1StreamAdapter &adapter, uint32_t pc)
        {
            if (!lifted(identify(adapter), pc))
            {
                ++g_state.sandboxed;
                return Vu1StreamAdapter::Choice::Sandbox;
            }
            return Vu1StreamAdapter::Choice::Port;
        }

        void draw(Vu1StreamAdapter &adapter, uint32_t pc, bool resumed)
        {
            State &s = g_state;
            ps2x::native::GifToNative &gif = adapter.sandbox().gif();
            gif.setSource(pc);
            const uint32_t start = resumed ? adapter.sandbox().vu().state().pc : pc;
            const Program *program = s.program;
            if (!lifted(program, start))
            {
                runSandbox(adapter, pc, resumed);
                gif.flush();
                return;
            }
            ++(resumed ? s.resumed : s.ported);
            if (s.mode == Mode::Check)
                check(adapter, *program, start, resumed);
            else
                adapter.addPortCycles(runLifted(adapter, *program, start, kickToGif, &gif));
            gif.flush();
        }
    }

    bool installVu1Port(Vu1StreamAdapter::Config &config)
    {
        const char *value = std::getenv("FFOP_VU1_PORT");
        if (!value || !*value || std::strcmp(value, "0") == 0)
            return false;
        g_state.mode = std::strcmp(value, "check") == 0 ? Mode::Check : Mode::On;
        config.choose = choose;
        config.draw = draw;
        std::fprintf(stderr, "[ffop] native VU1: %zu lifted program(s) %s\n", std::size(kPrograms),
                     g_state.mode == Mode::Check ? "checked against the sandbox" : "in the sandbox's place");
        return true;
    }

    void reportVu1Port()
    {
        State &s = g_state;
        if (s.mode == Mode::Off || ++s.frames % 300u != 0u)
            return;
        std::fprintf(stderr, "[ffop:vu1] lifted %llu MSCAL %llu MSCNT, sandbox %llu, checked %llu, differing %llu\n",
                     static_cast<unsigned long long>(s.ported), static_cast<unsigned long long>(s.resumed),
                     static_cast<unsigned long long>(s.sandboxed), static_cast<unsigned long long>(s.checked),
                     static_cast<unsigned long long>(s.mismatches));
    }
}
