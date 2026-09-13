/*
 * test_hello.c is a freestanding x86-64 ELF fixture for AcePS boot tests.
 * Build with: gcc -nostdlib -static -o test_hello tests/test_hello.c
 */

static long syscall1(long number, long argument) {
  long result;
  __asm__ volatile("syscall"
                   : "=a"(result)
                   : "a"(number), "D"(argument)
                   : "rcx", "r11", "memory");
  return result;
}

static long syscall3(long number, long argument0, long argument1, long argument2) {
  long result;
  __asm__ volatile("syscall"
                   : "=a"(result)
                   : "a"(number), "D"(argument0), "S"(argument1), "d"(argument2)
                   : "rcx", "r11", "memory");
  return result;
}

void _start(void) {
  static const char message[] = "AcePS: Hello\n";
  (void)syscall3(4, 1, (long)message, 13);
  (void)syscall1(1, 0);
  for (;;) {
    __asm__ volatile("pause");
  }
}
