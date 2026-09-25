#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

// These symbols come from the actual Loader/chrome_dll.asm forwarders. Test
// arbitrary signatures because they promise transparent ABI forwarding.
extern "C" double GetApplyHookResult(double, double, double, double, double, double);
extern "C" uint64_t GetHandleVerifier(uint64_t, double, uint64_t, double, uint64_t, double);

static double floats(double a, double b, double c, double d, double e, double f)
{
    return a + 2*b + 3*c + 4*d + 5*e + 6*f;
}
static uint64_t mixed(uint64_t a, double b, uint64_t c, double d, uint64_t e, double f)
{
    return a + 2*uint64_t(b) + 3*c + 4*uint64_t(d) + 5*e + 6*uint64_t(f);
}
extern "C" void* ResolveTestAPI(const char* name)
{
    if (!strcmp(name, "GetApplyHookResult")) return reinterpret_cast<void*>(floats);
    if (!strcmp(name, "GetHandleVerifier")) return reinterpret_cast<void*>(mixed);
    std::abort();
}

int main()
{
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) threads.emplace_back([t] {
        for (int i = 0; i < 1000; ++i) {
            if (GetApplyHookResult(t+1, 2, 3, 4, 5, i+6) != floats(t+1, 2, 3, 4, 5, i+6) ||
                GetHandleVerifier(t+1, 2, 3, 4, 5, i+6) != mixed(t+1, 2, 3, 4, 5, i+6)) {
                std::fprintf(stderr, "Loader corrupted register or stack arguments\n");
                std::abort();
            }
        }
    });
    for (auto& thread : threads) thread.join();
    std::puts("PASS: 16000 concurrent loader forwards preserve integer, floating-point and stack arguments.");
}
