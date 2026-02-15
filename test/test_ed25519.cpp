#include <Arduino.h>
#include <unity.h>
#include <monocypher.h>

void test_ed25519_sign_verify(void) {
    const uint8_t sk[32] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                             0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                             0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                             0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f };

    uint8_t pk[32];
    crypto_ed25519_public_key(pk, sk);

    const uint8_t msg[] = "unit test message";
    uint8_t sig[64];

    crypto_ed25519_sign(sig, msg, sizeof(msg)-1, sk);

    int ok = crypto_ed25519_verify(sig, msg, sizeof(msg)-1, pk);
    TEST_ASSERT_EQUAL_INT(0, ok);
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_ed25519_sign_verify);
    UNITY_END();
}

void loop() {}
