#include <Arduino.h>
#include <WiFi.h>
#include <unity.h>
#include "call_audio.h"
#include "hw.h"
#include "nvs_store.h"
#include "pins.h"
#include "tcp_listener.h"

void setUp() {}
void tearDown() {}
void test_answer_destination_in_scratch_nvs() {
    hw::NvsStore store("v09modem", "cfg");
    TEST_ASSERT_TRUE(store.erase());
    atcmd::Modem modem(store, nullptr, 0);
    modem.begin();
    TEST_ASSERT_EQUAL(int(modemcfg::AnswerPort::kAuto), int(modem.answerPort()));
    TEST_ASSERT_TRUE(modem.setAnswerPort(modemcfg::AnswerPort::kUsb));
    modem.active().s[0] = 3;
    modem.active().s[1] = 2;
    TEST_ASSERT_TRUE(modem.writeTemplate(0, 115200));
    atcmd::Modem again(store, nullptr, 0);
    again.begin();
    TEST_ASSERT_EQUAL(int(modemcfg::AnswerPort::kUsb), int(again.answerPort()));
    TEST_ASSERT_EQUAL(3, again.active().s[0]);
    TEST_ASSERT_EQUAL(0, again.active().s[1]);
    again.reset(4);
    TEST_ASSERT_EQUAL(int(modemcfg::AnswerPort::kUsb), int(again.answerPort()));
    TEST_ASSERT_TRUE(store.erase());
    atcmd::Modem fresh(store, nullptr, 0);
    fresh.begin();
    TEST_ASSERT_EQUAL(int(modemcfg::AnswerPort::kAuto), int(fresh.answerPort()));
}
void test_nonblocking_listener_accept_reject_close_and_reopen() {
    // Loopback uses the real lwIP adapter without a saved network or external peer.
    hw::TcpSocket accepted, client, extra;
    hw::TcpListener listener(accepted);
    for (unsigned cycle = 0; cycle < 8; ++cycle) {
        TEST_ASSERT_TRUE(listener.begin(32322));
        client.begin("127.0.0.1", 32322);
        bool got = false;
        uint32_t before = millis();
        while (millis() - before < 2000 && !got) {
            client.poll();
            got = listener.accept();
            delay(1);
        }
        TEST_ASSERT_TRUE(got);
        before = millis();
        while (millis() - before < 2000 && client.state() == calls::SocketState::kPending) {
            client.poll();
            delay(1);
        }
        TEST_ASSERT_EQUAL(int(calls::SocketState::kOpen), int(client.state()));
        TEST_ASSERT_EQUAL(int(calls::SocketState::kOpen), int(accepted.state()));
        const uint8_t payload[] = {0, 1, 255, 13, 10, 'A'};
        TEST_ASSERT_EQUAL(sizeof payload, client.write(payload, sizeof payload));
        uint8_t received[sizeof payload] = {};
        size_t count = 0;
        before = millis();
        while (millis() - before < 2000 && count < sizeof payload) {
            count += accepted.read(received + count, sizeof payload - count);
            delay(1);
        }
        TEST_ASSERT_EQUAL(sizeof payload, count);
        TEST_ASSERT_EQUAL_MEMORY(payload, received, sizeof payload);
        TEST_ASSERT_EQUAL(sizeof payload, accepted.write(payload, sizeof payload));
        count = 0;
        before = millis();
        while (millis() - before < 2000 && count < sizeof payload) {
            count += client.read(received + count, sizeof payload - count);
            delay(1);
        }
        TEST_ASSERT_EQUAL(sizeof payload, count);
        TEST_ASSERT_EQUAL_MEMORY(payload, received, sizeof payload);
        extra.begin("127.0.0.1", 32322);
        before = millis();
        while (millis() - before < 100) {
            listener.reject();
            extra.poll();
            delay(1);
        }
        TEST_ASSERT_EQUAL(int(calls::SocketState::kClosed), int(extra.state()));
        TEST_ASSERT_EQUAL(int(calls::SocketState::kOpen), int(accepted.state()));
        client.close();
        before = millis();
        while (millis() - before < 100 && accepted.state() == calls::SocketState::kOpen) {
            accepted.poll();
            delay(1);
        }
        TEST_ASSERT_EQUAL(int(calls::SocketState::kClosed), int(accepted.state()));
        listener.stop();
    }
}
void test_ring_audio_pot_and_shutdown_without_external_amp() {
    hw::CallAudio audio;
    modemcfg::Profile profile = modemcfg::factoryProfile(1);
    TEST_ASSERT_TRUE(audio.start(calls::SoundStage::kRing, "", false, 0, profile));
    uint32_t before = millis();
    while (millis() - before < 100) {
        audio.poll(millis(), profile);
        delay(1);
    }
    TEST_ASSERT_TRUE(audio.running());
    TEST_ASSERT_FALSE(audio.failed());
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(hw::kPotMax, hw::potRaw());
    audio.stop();
    TEST_ASSERT_FALSE(hw::ampIsOn());
}
void test_ri_gpio_is_active_low_and_released() {
    hw::linesBegin();
    hw::lineSet(hwtest::OutSignal::kRI, true);
    TEST_ASSERT_EQUAL(LOW, digitalRead(pins::kRi));
    hw::lineSet(hwtest::OutSignal::kRI, false);
    TEST_ASSERT_EQUAL(HIGH, digitalRead(pins::kRi));
}
void setup() {
    delay(2000);
    WiFi.mode(WIFI_STA);
    UNITY_BEGIN();
    RUN_TEST(test_answer_destination_in_scratch_nvs);
    RUN_TEST(test_nonblocking_listener_accept_reject_close_and_reopen);
    RUN_TEST(test_ring_audio_pot_and_shutdown_without_external_amp);
    RUN_TEST(test_ri_gpio_is_active_low_and_released);
    UNITY_END();
}
void loop() {}
