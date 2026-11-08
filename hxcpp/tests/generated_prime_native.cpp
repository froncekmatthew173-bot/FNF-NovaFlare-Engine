#include <hx/CFFI.h>

int nova_generated_prime_add(int left, int right) {
    return left + right;
}

HxString nova_generated_prime_echo(HxString input) {
    return input;
}

DEFINE_PRIME2(nova_generated_prime_add);
DEFINE_PRIME1(nova_generated_prime_echo);
