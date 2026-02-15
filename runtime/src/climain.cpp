extern "C" void __thg_init_env(int c, char **v);
extern "C" int __thg_cli_main_native();

int main(int argc, char **argv) {
  __thg_init_env(argc, argv);
  return __thg_cli_main_native();
}
