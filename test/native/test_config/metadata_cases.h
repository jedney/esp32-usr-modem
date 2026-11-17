#pragma once
#include <cstdlib>
#include "catalog.h"
#include "hwtest.h"
#include "net_tz.h"
#include "tones.h"
#include "ui_generated.h"

void test_metadata_json_matches_the_generated_source() {
    FILE *source = fopen("lib/config/ui-bindings.json", "rb");
    TEST_ASSERT_NOT_NULL(source);
    uint32_t hash = 2166136261u;
    int byte;
    while ((byte = fgetc(source)) != EOF)
        if (byte != '\r')
            hash = (hash ^ uint8_t(byte)) * 16777619u;
    fclose(source);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(kBindingsFingerprint, hash,
                                     "Metadata source changed; run scripts/generate_ui.py");
}

const UiMetadata &metadata(const char *id) {
    size_t count;
    const Action *rows = actions(count);
    for (size_t i = 0; i < count; ++i)
        if (!strcmp(rows[i].id, id))
            return *rows[i].metadata;
    TEST_FAIL_MESSAGE(id);
    return *rows[0].metadata;
}
const UiMetadata &field(const UiMetadata &node, const char *id) {
    for (size_t i = 0; i < node.fieldCount; ++i)
        if (!strcmp(node.fields[i].id, id))
            return node.fields[i];
    TEST_FAIL_MESSAGE(id);
    return node;
}
bool hasValue(const UiMetadata &node, const char *value) {
    for (size_t i = 0; i < node.valueCount; ++i)
        if (!strcmp(node.values[i].value, value))
            return true;
    return false;
}
bool hasNumber(const UiMetadata &node, unsigned value) {
    char text[16];
    snprintf(text, sizeof text, "%u", value);
    return hasValue(node, text);
}
void checkTemplates(const UiMetadata &node, atcmd::Modem &modem) {
    TEST_ASSERT_TRUE(*node.friendly);
    TEST_ASSERT_TRUE(*node.help);
    if (*node.commandTemplate) {
        TEST_ASSERT_EQUAL('*', node.commandTemplate[0]);
        const char *args;
        auto *entry = modem.findStar(node.commandTemplate + 1, &args);
        TEST_ASSERT_NOT_NULL_MESSAGE(entry, node.commandTemplate);
        TEST_ASSERT_NOT_NULL_MESSAGE(entry->fn, node.commandTemplate);
    }
    for (size_t i = 0; i < node.valueCount; ++i) {
        TEST_ASSERT_TRUE(*node.values[i].label);
        TEST_ASSERT_TRUE(*node.values[i].description);
    }
    for (size_t i = 0; i < node.fieldCount; ++i)
        checkTemplates(node.fields[i], modem);
    for (size_t i = 0; i < node.variantCount; ++i)
        checkTemplates(node.variants[i], modem);
}
void test_metadata_enumerations_match_firmware_tables() {
    const auto &models = metadata("settings.model");
    TEST_ASSERT_EQUAL(4, models.valueCount);
    for (unsigned speed = 0; speed <= 60000; ++speed)
        TEST_ASSERT_EQUAL(modemcfg::validModel(speed), hasNumber(models, speed));
    for (size_t i = 0; i < models.valueCount; ++i)
        TEST_ASSERT_EQUAL_STRING(modemcfg::productName(atoi(models.values[i].value)),
                                 models.values[i].label);
    const auto &baud = metadata("settings.host-baud");
    TEST_ASSERT_EQUAL(std::size(modemcfg::kHostRates) + 1, baud.valueCount);
    TEST_ASSERT_TRUE(hasValue(baud, "AUTO"));
    for (unsigned speed : modemcfg::kHostRates)
        TEST_ASSERT_TRUE(hasNumber(baud, speed));
    const auto &answer = metadata("settings.auto-answer-destination");
    TEST_ASSERT_EQUAL(modemcfg::kAnswerPortCount, answer.valueCount);
    for (unsigned i = 0; i < modemcfg::kAnswerPortCount; ++i)
        TEST_ASSERT_TRUE(hasValue(answer, modemcfg::answerPortName(modemcfg::AnswerPort(i))));
    const auto &zones = metadata("wifi-clock.time-zone");
    TEST_ASSERT_EQUAL(netcfg::tz::zoneCount(), zones.valueCount);
    for (size_t i = 0; i < netcfg::tz::zoneCount(); ++i)
        TEST_ASSERT_TRUE(hasValue(zones, netcfg::tz::zoneName(i)));
    TEST_ASSERT_TRUE(zones.allowCustom);
    const auto &play = metadata("sounds.play-sound");
    const auto &speed = field(metadata("phonebook.add-edit-entry"), "speed");
    TEST_ASSERT_EQUAL(modemcfg::kMaxLineSpeedIndex + 6, play.valueCount);
    TEST_ASSERT_EQUAL(modemcfg::kMaxLineSpeedIndex + 1, speed.valueCount);
    TEST_ASSERT_TRUE(hasNumber(speed, 0));
    for (unsigned i = 1; i <= modemcfg::kMaxLineSpeedIndex; ++i) {
        TEST_ASSERT_TRUE(hasNumber(speed, atcmd::lineSpeedForN(i)));
        TEST_ASSERT_TRUE(hasNumber(play, atcmd::lineSpeedForN(i)));
    }
    for (const auto &asset : sounds::kAssets)
        TEST_ASSERT_TRUE(hasNumber(play, asset.speed));
    const auto &leds = metadata("hardware-tests.leds");
    TEST_ASSERT_EQUAL(hwtest::kLedCount + 5, leds.valueCount);
    for (unsigned i = 0; i < hwtest::kLedCount; ++i)
        TEST_ASSERT_TRUE(hasValue(leds, hwtest::ledName(i)));
    const auto &proto = field(metadata("phonebook.add-edit-entry"), "proto");
    TEST_ASSERT_EQUAL(calls::kProtocolCount, proto.valueCount);
    for (unsigned i = 0; i < calls::kProtocolCount; ++i)
        TEST_ASSERT_TRUE(hasValue(proto, calls::protocolName(calls::Protocol(i))));
}
void test_metadata_profile_values_and_registers_match_firmware() {
    const auto &profile = metadata("settings.edit-profile-registers");
    TEST_ASSERT_EQUAL(modemcfg::kSettingCount + modemcfg::kSRegCount, profile.fieldCount);
    for (unsigned i = 0; i < modemcfg::kSettingCount; ++i) {
        auto setting = modemcfg::Setting(i);
        const auto &f = field(profile, modemcfg::settingInfo(setting).name);
        unsigned count = 0;
        for (unsigned v = 0; v < 64; ++v) {
            bool inMetadata =
                i == modemcfg::kTone ? (v < 2 && hasValue(f, v ? "T" : "P")) : hasNumber(f, v);
            TEST_ASSERT_EQUAL_MESSAGE(modemcfg::settingAllows(setting, v), inMetadata, f.id);
            count += modemcfg::settingAllows(setting, v);
        }
        TEST_ASSERT_EQUAL(count, f.valueCount);
    }
    for (unsigned reg = 0; reg < modemcfg::kSRegCount; ++reg) {
        char name[8];
        snprintf(name, sizeof name, "S%u", reg);
        const auto &f = field(profile, name);
        TEST_ASSERT_EQUAL(reg == 1, f.readOnly);
        for (unsigned v = 0; v <= 256; ++v) {
            bool allowed = f.type == UiType::Enum ? hasNumber(f, v)
                                                  : v >= unsigned(f.min) && v <= unsigned(f.max);
            TEST_ASSERT_EQUAL_MESSAGE(v <= 255 && modemcfg::sRegisterAllowed(reg, v), allowed,
                                      name);
        }
    }
    TEST_ASSERT_EQUAL(modemcfg::kTemplates, metadata("settings.save-active-profile").valueCount);
    TEST_ASSERT_EQUAL(modemcfg::kTemplates + 3, metadata("settings.power-on-profile").valueCount);
    TEST_ASSERT_EQUAL(modemcfg::kPhoneSlots - 1,
                      field(metadata("settings.stored-number"), "slot").max);
    TEST_ASSERT_EQUAL(modemcfg::kPhoneLen,
                      field(metadata("settings.stored-number"), "number").maxLength);
}
bool matchesLength(const UiMetadata &node, const std::string &text) {
    return (node.minLength < 0 || text.size() >= size_t(node.minLength)) &&
           (node.maxLength < 0 || text.size() <= size_t(node.maxLength));
}
void test_metadata_limits_match_firmware() {
    for (const char *id : {"sounds.ring-delay", "sounds.connect-delay"}) {
        const auto &node = metadata(id);
        TEST_ASSERT_EQUAL(0, node.min);
        TEST_ASSERT_EQUAL(modemcfg::kMaxCallDelayMs, node.max);
        TEST_ASSERT_EQUAL_STRING("milliseconds", node.unit);
        TEST_ASSERT_EQUAL_STRING("seconds", node.displayUnit);
        TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.001f, node.displayScale);
    }
    const auto &volume = field(metadata("hardware-tests.test-volume"), "percent");
    TEST_ASSERT_EQUAL(0, volume.min);
    TEST_ASSERT_EQUAL(100, volume.max);
    const auto &tone = metadata("hardware-tests.tone-sweep");
    TEST_ASSERT_EQUAL(tones::kMinToneHz, field(tone, "hz").min);
    TEST_ASSERT_EQUAL(tones::kMaxToneHz, field(tone, "hz").max);
    const auto &book = metadata("phonebook.add-edit-entry");
    TEST_ASSERT_EQUAL(sizeof(calls::Entry{}.number) - 1, field(book, "num").maxLength);
    TEST_ASSERT_EQUAL(sizeof(calls::Entry{}.host) - 1, field(book, "host").maxLength);
    TEST_ASSERT_EQUAL(1, field(book, "port").min);
    TEST_ASSERT_EQUAL(UINT16_MAX, field(book, "port").max);
    TEST_ASSERT_EQUAL(netcfg::kSsidMax, field(metadata("wifi-clock.join"), "ssid").maxLength);
    TEST_ASSERT_EQUAL(netcfg::Net::kScanMax,
                      field(metadata("wifi-clock.join").variants[1], "index").max);
    const auto &hostname = metadata("wifi-clock.network-name");
    const auto &ntp = metadata("wifi-clock.ntp-server");
    const auto &password = metadata("updates-reset.change-device-password");
    TEST_ASSERT_EQUAL(netcfg::kHostMax, hostname.maxLength);
    TEST_ASSERT_EQUAL(netcfg::kNtpMax, ntp.maxLength);
    TEST_ASSERT_EQUAL(netcfg::kTzMax, metadata("wifi-clock.time-zone").maxLength);
    TEST_ASSERT_EQUAL(devicecfg::kPasswordMax, password.maxLength);
    TEST_ASSERT_EQUAL(field(metadata("settings.edit-profile-registers"), "S0").min,
                      metadata("settings.auto-answer-rings").min);
    TEST_ASSERT_EQUAL(field(metadata("settings.edit-profile-registers"), "S0").max,
                      metadata("settings.auto-answer-rings").max);
    for (unsigned len = 0; len < 70; ++len) {
        std::string value(len, 'a');
        char normalized[netcfg::kHostMax + 1];
        TEST_ASSERT_EQUAL(netcfg::normalizeHost(value.c_str(), normalized, sizeof normalized),
                          matchesLength(hostname, value));
        TEST_ASSERT_EQUAL(netcfg::validServer(value.c_str()), matchesLength(ntp, value));
        TEST_ASSERT_EQUAL(devicecfg::validPassword(value.c_str()), matchesLength(password, value));
    }
}
