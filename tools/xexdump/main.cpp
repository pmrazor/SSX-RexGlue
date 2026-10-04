// Dumps the loaded (decrypted/decompressed) XEX image from guest memory for disassembly.
#include <cstdio>
#include <memory>
#include <string>

#include <rex/kernel/init.h>
#include <rex/memory.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: xexdump <game_root> <xex_rel_path> <out.bin>\n");
    return 1;
  }
  auto runtime = std::make_unique<rex::Runtime>(argv[1]);
  if (runtime->Setup(rex::RuntimeConfig{.kernel_init = rex::kernel::InitializeKernel,
                                        .tool_mode = true}) != 0)
    return 2;
  if (runtime->LoadXexImage(std::string("game:\\") + argv[2]) != 0)
    return 3;
  auto* xex = runtime->kernel_state()->GetExecutableModule()->xex_module();
  uint32_t base = xex->base_address(), size = xex->image_size();
  FILE* f = std::fopen(argv[3], "wb");
  std::fwrite(runtime->memory()->TranslateVirtual(base), 1, size, f);
  std::fclose(f);
  std::printf("base=0x%08X size=0x%08X\n", base, size);
  return 0;
}
