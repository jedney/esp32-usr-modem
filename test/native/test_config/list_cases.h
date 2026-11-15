#pragma once
bool contains(const std::string &s, const char *text) {
    return s.find(text) != std::string::npos;
}
void test_lists_use_only_letters_and_sgr_at_every_size() {
    for (const char *style : {"PLAIN", "ANSI"})
        for (unsigned width : {1u, 19u, 40u, 79u, 80u, 120u, 132u}) {
            Rig r;
            TEST_ASSERT_TRUE(r.terminal.setStyle(style));
            r.terminal.resize(width, 8);
            r.terminal.open();
            auto text = r.drain();
            TEST_ASSERT_TRUE(contains(text, "A. Status"));
            TEST_ASSERT_TRUE(contains(text, "M. Command Reference"));
            TEST_ASSERT_FALSE(contains(text, "[18t"));
            for (size_t i = 0; i < text.size(); ++i)
                if (text[i] == 27) {
                    TEST_ASSERT_EQUAL_STRING("ANSI", style);
                    if (i + 1 >= text.size()) {
                        TEST_FAIL_MESSAGE("Truncated ANSI escape");
                        return;
                    }
                    TEST_ASSERT_EQUAL('[', text[++i]);
                    while (i + 1 < text.size() && (isdigit(text[i + 1]) || text[i + 1] == ';'))
                        ++i;
                    if (i + 1 >= text.size()) {
                        TEST_FAIL_MESSAGE("Unterminated SGR colour");
                        return;
                    }
                    TEST_ASSERT_EQUAL('m', text[++i]);
                }
            r.feed("\x1b[B\x1bOC1");
            TEST_ASSERT_TRUE(r.drain().empty());
            r.feed("c");
            TEST_ASSERT_TRUE(contains(r.drain(), "Rings before answering"));
        }
}
void test_every_section_and_profile_page_is_reachable() {
    Rig r;
    r.terminal.open();
    r.drain();
    for (char key : {'B', 'C', 'D', 'E', 'G', 'H', 'K', 'L'}) {
        r.feed(std::string(1, key).c_str());
        TEST_ASSERT_FALSE(r.drain().empty());
        r.feed("x");
        r.drain();
    }
    r.feed("cb");
    auto text = r.drain();
    TEST_ASSERT_TRUE(contains(text, "Command echo"));
    for (unsigned i = 0; i < 5; ++i) {
        r.feed("r");
        text = r.drain();
    }
    TEST_ASSERT_TRUE(contains(text, "S63"));
    r.feed("p");
    TEST_ASSERT_FALSE(r.drain().empty());
}
void test_enum_choices_show_friendly_labels_and_confirm() {
    Rig r;
    r.terminal.open();
    r.drain();
    r.feed("cc");
    auto text = r.drain();
    TEST_ASSERT_TRUE(contains(text, "A. US Robotics Sportster 14.4k"));
    TEST_ASSERT_TRUE(contains(text, "B. US Robotics Sportster 28.8k"));
    r.feed("b\r");
    TEST_ASSERT_TRUE(contains(r.drain(), "Y confirms"));
    TEST_ASSERT_EQUAL(14400, r.modem.model());
    r.feed("n");
    r.drain();
    TEST_ASSERT_EQUAL(14400, r.modem.model());
    r.feed("cc\r");
    r.drain();
    r.feed("y\r");
    r.drain();
    TEST_ASSERT_EQUAL(33600, r.modem.model());
}
void test_forms_echo_only_new_text_and_mask_secrets() {
    Rig r;
    r.terminal.open();
    r.drain();
    r.feed("hd");
    r.drain();
    r.feed("SecretCanary123");
    auto text = r.drain();
    TEST_ASSERT_FALSE(contains(text, "SecretCanary"));
    TEST_ASSERT_TRUE(contains(text, "***"));
    r.feed("\r");
    TEST_ASSERT_FALSE(contains(r.drain(), "SecretCanary"));
    r.terminal.poll(30001);
    TEST_ASSERT_TRUE(contains(r.drain(), "timed out"));
    r.feed("xgk");
    r.drain();
    r.feed("example.com");
    TEST_ASSERT_EQUAL_STRING("example.com", r.drain().c_str());
    r.feed("\x1b[D");
    TEST_ASSERT_TRUE(r.drain().empty());
    r.feed("\x1bz");
    TEST_ASSERT_TRUE(r.drain().empty());
    r.feed("\x1b");
    r.terminal.poll(30100);
    TEST_ASSERT_TRUE(contains(r.drain(), "cancelled"));
}
void test_async_password_and_geometry_are_isolated() {
    Rig r;
    TEST_ASSERT_FALSE(r.terminal.setSize("4294967376,25"));
    TEST_ASSERT_FALSE(r.terminal.setSize("80,25junk"));
    r.terminal.open();
    r.drain();
    stubAsync = true;
    r.feed("kb");
    auto text = r.drain();
    TEST_ASSERT_TRUE(contains(text, "Password:"));
    TEST_ASSERT_TRUE(r.terminal.capturing());
    r.feed("\x1b[8;45;120t");
    TEST_ASSERT_EQUAL(120, r.terminal.columns());
    r.feed("PrivateCanary");
    TEST_ASSERT_FALSE(contains(r.drain(), "PrivateCanary"));
    r.feed("\r");
    r.terminal.poll(100);
    TEST_ASSERT_EQUAL_STRING("PrivateCanary", secretReceived.c_str());
    TEST_ASSERT_FALSE(r.terminal.capturing());
    stubWait = true;
    r.feed("xkb");
    r.drain();
    r.feed("\x1b");
    r.terminal.poll(200);
    TEST_ASSERT_TRUE(waitCancelled);
    TEST_ASSERT_FALSE(r.terminal.capturing());
}
void test_output_pages_are_complete_and_sanitized() {
    Rig r;
    r.terminal.setStyle("PLAIN");
    r.terminal.open();
    r.drain();
    for (unsigned i = 0; i < 45; ++i) {
        char row[32];
        snprintf(row, sizeof row, "row%02u\n", i);
        r.terminal.write(row, strlen(row));
    }
    auto text = r.drain();
    TEST_ASSERT_TRUE(contains(text, "row14"));
    TEST_ASSERT_FALSE(contains(text, "row15"));
    r.feed("r");
    text = r.drain();
    TEST_ASSERT_TRUE(contains(text, "row15"));
    TEST_ASSERT_FALSE(contains(text, "row14"));
    r.feed("p");
    TEST_ASSERT_TRUE(contains(r.drain(), "row00"));
    r.feed("x");
    r.drain();
    r.terminal.write("\r\nfirst\n\x1b[2Jhello\n", 18);
    text = r.drain();
    TEST_ASSERT_TRUE(contains(text, "first\r\n?[2Jhello"));
    TEST_ASSERT_FALSE(contains(text, "\x1b[2J"));
    r.feed("x");
    r.drain();
    std::string huge(100000, 'x');
    r.terminal.write(huge.data(), huge.size());
    text = r.drain(64);
    TEST_ASSERT_TRUE(contains(text, "Output limit reached"));
}
void test_disabled_actions_and_browser_diagnostics() {
    Rig r(true);
    r.terminal.open();
    r.drain();
    r.feed("k");
    TEST_ASSERT_TRUE(contains(r.drain(), "unavailable: takes over RS232"));
    stubCalls = 0;
    r.feed("l");
    TEST_ASSERT_TRUE(contains(r.drain(), "Unavailable"));
    TEST_ASSERT_EQUAL(0, stubCalls);
    Rig browser;
    browser.session.port().setBrowser(true);
    browser.terminal.open();
    browser.drain();
    browser.feed("la");
    TEST_ASSERT_TRUE(contains(browser.drain(), "Client Tests run on serial only"));
    TEST_ASSERT_EQUAL(0, stubCalls);
}
void test_idle_cancel_timeout_and_reservation() {
    Rig r;
    r.terminal.open();
    TEST_ASSERT_TRUE(r.session.startLocal(r.terminal));
    r.drain();
    r.terminal.poll(6000);
    TEST_ASSERT_TRUE(r.drain().empty());
    r.terminal.resize(40, 10);
    TEST_ASSERT_TRUE(r.drain().empty());
    r.feed("hd");
    r.drain();
    r.feed("temporary");
    r.drain();
    r.terminal.poll(126001);
    TEST_ASSERT_TRUE(contains(r.drain(), "timed out"));
    r.terminal.poll(305999);
    TEST_ASSERT_TRUE(r.terminal.isOpen());
    r.terminal.poll(306001);
    r.session.tick(306001, true);
    TEST_ASSERT_FALSE(r.slot.busy());
}
void test_structured_phonebook_edit_and_confirmation() {
    Rig r;
    BookServices services;
    r.terminal.services(&services);
    r.terminal.open();
    r.drain();
    r.feed("bb");
    auto text = r.drain();
    TEST_ASSERT_TRUE(contains(text, "Phonebook number"));
    r.feed("42\rbbs.example.com\r23\ra\ra\r");
    text = r.drain();
    TEST_ASSERT_TRUE(contains(text, "Y confirms"));
    TEST_ASSERT_NULL(services.book.find("42"));
    r.feed("y");
    r.drain();
    TEST_ASSERT_NOT_NULL(services.book.find("42"));
    TEST_ASSERT_EQUAL_STRING("bbs.example.com", services.book.find("42")->host);
    r.feed("xc42\r");
    TEST_ASSERT_TRUE(contains(r.drain(), "Y confirms"));
    auto changed = *services.book.find("42");
    strcpy(changed.host, "changed.example");
    services.book.put(changed);
    r.feed("y");
    TEST_ASSERT_TRUE(contains(r.drain(), "Entry changed"));
    TEST_ASSERT_NOT_NULL(services.book.find("42"));
}
void test_metadata_units_ranges_and_current_values() {
    Rig r;
    r.terminal.setStyle("PLAIN");
    r.terminal.open();
    r.drain();
    r.feed("d");
    auto text = r.drain();
    TEST_ASSERT_TRUE(contains(text, "1.5 seconds"));
    r.feed("c");
    text = r.drain();
    TEST_ASSERT_TRUE(contains(text, "0-60 seconds"));
    r.feed("1.25\r");
    r.drain();
    r.feed("y");
    r.drain();
    TEST_ASSERT_EQUAL(1250, r.modem.ringDelayMs());
    r.feed("xxgk");
    r.drain();
    r.feed(
        "123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890"
        "1234567890123456789012345678901234567890123456789012345678901234567890\r");
    TEST_ASSERT_TRUE(contains(r.drain(), "Input too long"));
    TEST_ASSERT_EQUAL(14400, r.modem.model());
}
void test_binary_diagnostics_have_no_menu_bytes() {
    class Raw : public hostport::Connection {
    public:
        unsigned position = 0;
        bool active = true;
        bool isOpen() const override { return active; }
        bool binaryService() const override { return true; }
        size_t space() const override { return 256; }
        size_t write(const uint8_t *, size_t n) override { return n; }
        size_t read(uint8_t *out, size_t cap) override {
            size_t n = std::min<size_t>(cap, 256 - position);
            for (size_t i = 0; i < n; ++i)
                out[i] = position++;
            return n;
        }
        void close() override { active = false; }
    } raw;
    Rig r;
    r.terminal.open();
    r.drain();
    r.terminal.diagnostic(raw);
    r.terminal.poll(10000);
    auto bytes = r.drain();
    TEST_ASSERT_EQUAL(256, bytes.size());
    for (unsigned i = 0; i < 256; ++i)
        TEST_ASSERT_EQUAL_HEX8(i, uint8_t(bytes[i]));
    raw.close();
    r.terminal.poll(10001);
    TEST_ASSERT_FALSE(r.drain().empty());
}
void test_optional_fields_variants_and_register_edits() {
    Rig r;
    BookServices services;
    r.terminal.services(&services);
    r.terminal.open();
    r.drain();
    r.feed("bb42\rbbs.example.com\r23\ra\r\r");
    TEST_ASSERT_TRUE(contains(r.drain(), "Y confirms"));
    r.feed("y");
    r.drain();
    TEST_ASSERT_NOT_NULL(services.book.find("42"));
    TEST_ASSERT_EQUAL(0, services.book.find("42")->speed);
    r.feed("xxcb");
    r.drain();
    r.feed("b");
    r.drain();  // command echo, a profile enumeration
    r.feed("a\r");
    TEST_ASSERT_TRUE(contains(r.drain(), "*SETTING=E0"));
    r.feed("n");
    r.drain();
    r.feed("rk");
    r.drain();  // page 2: S0
    r.feed("4\r");
    TEST_ASSERT_TRUE(contains(r.drain(), "*SETTING=S0=4"));
    r.feed("n");
    r.drain();
    r.feed("l");
    TEST_ASSERT_TRUE(contains(r.drain(), "read only"));  // S1
    r.feed("xxgf");
    r.drain();  // IP configuration variants
    r.feed("a");
    TEST_ASSERT_TRUE(contains(r.drain(), "*WIFIIP=DHCP"));
    r.feed("n");
    r.drain();
}
void test_invalid_numbers_and_terminal_reports_cannot_change_state() {
    Rig r;
    r.terminal.open();
    r.drain();
    r.feed("dc");
    r.drain();
    for (const char *value : {"nan", "inf", "-1", "60.001", "0.0005", "1x"}) {
        r.feed(value);
        r.feed("\r");
        TEST_ASSERT_TRUE(contains(r.drain(), "within the displayed range"));
        TEST_ASSERT_EQUAL(1500, r.modem.ringDelayMs());
        r.feed("\x15");
        r.drain();
    }
    r.feed("\x1b[8;45;120x");
    TEST_ASSERT_EQUAL(80, r.terminal.columns());
    r.feed("\x1b[8;45;120t");
    TEST_ASSERT_EQUAL(120, r.terminal.columns());
    r.feed(("\x1b[" + std::string(80, '0') + "A").c_str());
    TEST_ASSERT_TRUE(r.terminal.isOpen());
}
