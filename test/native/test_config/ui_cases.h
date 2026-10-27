#pragma once
void test_serial_reports_and_async_prompts_keep_input_isolated() {
    Rig r;
    TEST_ASSERT_FALSE(r.terminal.setSize("4294967376,25"));
    TEST_ASSERT_FALSE(r.terminal.setSize("-80,25"));
    TEST_ASSERT_FALSE(r.terminal.setSize("80,25junk"));
    TEST_ASSERT_TRUE(r.terminal.setSize("80,25"));
    r.terminal.open();
    r.drain();
    stubAsync = true;
    secretReceived.clear();
    r.feed("a\r2\r");
    ScreenView view;
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("Password:"));
    TEST_ASSERT_TRUE(r.terminal.capturing());
    r.feed("\x1b[8;45;120t");
    TEST_ASSERT_EQUAL(120, r.terminal.columns());
    TEST_ASSERT_TRUE(r.terminal.capturing());
    r.feed("PrivateCanary");
    auto text = r.drain();
    TEST_ASSERT_EQUAL(std::string::npos, text.find("PrivateCanary"));
    r.feed("\r");
    r.terminal.poll(100);
    TEST_ASSERT_EQUAL_STRING("PrivateCanary", secretReceived.c_str());
    TEST_ASSERT_FALSE(r.terminal.capturing());
    auto malformed = "\x1b[" + std::string(80, '0') + "A";
    r.feed(malformed.c_str());
    TEST_ASSERT_TRUE(r.terminal.isOpen());
}
void test_phonebook_capacity_boundaries_and_wide_actions() {
    for (unsigned count : {0u, 1u, 9u, 10u, 64u}) {
        Rig r;
        BookServices services;
        for (unsigned i = 0; i < count; ++i)
            services.add(i);
        r.terminal.services(&services);
        r.terminal.open();
        r.drain();
        r.feed("2\r");
        ScreenView view;
        view.accept(r.drain());
        char page[32];
        snprintf(page, sizeof page, "PAGE 1/%u", std::max(1u, (count + 8) / 9));
        TEST_ASSERT_TRUE(view.has(page));
        if (!count)
            TEST_ASSERT_TRUE(view.has("[3] Delete [X]"));
        for (unsigned i = 9; i < count; i += 9) {
            r.feed("\x1b[6~");
            view.accept(r.drain());
        }
        TEST_ASSERT_EQUAL(0, view.outside);
        TEST_ASSERT_EQUAL(0, view.scrolls);
        if (count == 64) {
            r.terminal.setStyle("CP437");
            r.terminal.color(Color::Rgb);
            r.terminal.resize(120, 45);
            ScreenView wide(120, 45);
            wide.accept(r.drain());
            TEST_ASSERT_TRUE(wide.has("PHONEBOOK COMMANDS"));
            TEST_ASSERT_TRUE(wide.has("SELECTED ENTRY DETAILS"));
            TEST_ASSERT_TRUE(wide.has("host63.example"));
            TEST_ASSERT_EQUAL(0, wide.outside);
            TEST_ASSERT_EQUAL(0, wide.scrolls);
            wide.snapshot("phonebook-120x45");
        }
    }
}
void test_geometry_inventory_and_compact_submenus() {
    for (auto dimensions : {std::pair<unsigned, unsigned>{80, 25},
                            {79, 25},
                            {80, 24},
                            {100, 25},
                            {120, 45},
                            {132, 50},
                            {20, 8}}) {
        Rig r;
        r.modem.setModel(28800);
        r.terminal.setStyle("CP437");
        r.terminal.color(Color::Rgb);
        r.terminal.resize(dimensions.first, dimensions.second);
        r.terminal.open();
        ScreenView view(dimensions.first, dimensions.second);
        view.accept(r.drain(1));
        TEST_ASSERT_EQUAL(0, view.outside);
        TEST_ASSERT_EQUAL(0, view.scrolls);
        TEST_ASSERT_TRUE(view.has("Command >"));
        TEST_ASSERT_TRUE(view.wrap && view.cursor);
        char name[48];
        snprintf(name, sizeof name, "main-%ux%u", dimensions.first, dimensions.second);
        view.snapshot(name);
        if (dimensions.first >= 80 && dimensions.second >= 25) {
            size_t count;
            auto *a = actions(count);
            for (size_t i = 0; i < count; ++i)
                if (a[i].screen == Screen::Main)
                    TEST_ASSERT_TRUE_MESSAGE(view.has(a[i].label), a[i].label);
            if (dimensions.second >= 45) {
                TEST_ASSERT_NOT_EQUAL(std::string::npos, view.row(37).find("Serial terminal"));
                TEST_ASSERT_EQUAL(char(205), view.row(38)[dimensions.first / 2 + 3]);
            }
        } else
            TEST_ASSERT_TRUE(view.has("Small terminal"));
        r.feed("3\r");
        view.accept(r.drain());
        TEST_ASSERT_EQUAL(0, view.outside);
        TEST_ASSERT_EQUAL(0, view.scrolls);
    }
    size_t count;
    auto *a = actions(count);
    TEST_ASSERT_EQUAL(85, count);
    unsigned root = 0;
    for (size_t i = 0; i < count; ++i) {
        TEST_ASSERT_TRUE(*a[i].id);
        TEST_ASSERT_TRUE(a[i].key);
        if (a[i].screen == Screen::Main)
            ++root;
        for (size_t j = 0; j < i; ++j)
            TEST_ASSERT_NOT_EQUAL(0, strcmp(a[i].id, a[j].id));
    }
    TEST_ASSERT_EQUAL(13, root);
    for (char key : {'3', '7', 'A', 'D'}) {
        Rig r;
        r.terminal.setStyle("CP437");
        r.terminal.color(Color::Rgb);
        r.terminal.open();
        r.drain();
        char keys[] = {key, '\r', 0};
        r.feed(keys);
        ScreenView view;
        view.accept(r.drain());
        Screen screen = key == '3'   ? Screen::Settings
                        : key == '7' ? Screen::Wifi
                        : key == 'A' ? Screen::Hardware
                                     : Screen::Client;
        for (size_t i = 0; i < count; ++i)
            if (a[i].screen == screen)
                TEST_ASSERT_TRUE_MESSAGE(view.has(a[i].label), a[i].label);
        TEST_ASSERT_FALSE(view.has("More:"));
        view.snapshot(key == '3'   ? "settings"
                      : key == '7' ? "wifi"
                      : key == 'A' ? "hardware"
                                   : "client-tests");
    }
}
void test_four_themes_unknown_live_refresh_and_failed_save() {
    const unsigned speeds[] = {14400, 28800, 33600, 56000};
    const unsigned colors[] = {0x6f327f, 0x448fa1, 0x349b67, 0x3f668e};
    for (unsigned i = 0; i < 4; ++i) {
        Rig r;
        r.modem.setModel(speeds[i]);
        r.terminal.color(Color::Rgb);
        r.terminal.open();
        ScreenView view;
        view.accept(r.drain());
        TEST_ASSERT_TRUE(view.has(CellGrid::modelLabel(speeds[i])));
        unsigned x = (80 - strlen(CellGrid::modelLabel(speeds[i]))) / 2;
        TEST_ASSERT_EQUAL_HEX(colors[i], view.color(x, 6));
        r.feed("\x1b[C");
        r.drain();
        TEST_ASSERT_TRUE(r.modem.setModel(speeds[(i + 1) % 4]));
        r.terminal.poll(2100);
        view.accept(r.drain());
        TEST_ASSERT_TRUE(view.has(CellGrid::modelLabel(speeds[(i + 1) % 4])));
        TEST_ASSERT_TRUE(view.has("> [7]"));
        r.store.fail = true;
        TEST_ASSERT_FALSE(r.modem.setModel(speeds[i]));
        r.terminal.poll(4200);
        view.accept(r.drain());
        TEST_ASSERT_TRUE(view.has(CellGrid::modelLabel(speeds[(i + 1) % 4])));
    }
    Rig r;
    const_cast<modemcfg::Nvram &>(r.modem.nvram()).model = 123;
    r.terminal.open();
    ScreenView unknown;
    unknown.accept(r.drain());
    TEST_ASSERT_TRUE(unknown.has("Sportster model unknown"));
    TEST_ASSERT_FALSE(unknown.has("Sportster 14.4k Series"));
}
void test_disabled_navigation_back_stack_and_form_focus() {
    Rig r;
    r.terminal.open();
    r.drain();
    stubCalls = 0;
    r.feed("d\r");
    ScreenView view;
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("Client Tests"));
    TEST_ASSERT_EQUAL(0, stubCalls);
    r.feed("q\x1b[A");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("> [C] About"));
    r.feed("\x1b[B");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("> [D] Client Tests"));
    TEST_ASSERT_TRUE(view.has("> [D]"));
    r.feed("3\r2\r");
    r.drain();
    r.feed("\x1b");
    r.terminal.poll(100);
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("> [2] Edit profile"));
    r.feed("3\r33600\r");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("Y confirms"));
    TEST_ASSERT_EQUAL(14400, r.modem.model());
    r.feed("n\r");
    r.drain();
    r.feed("3\r33600\rY\r");
    view.accept(r.drain());
    TEST_ASSERT_EQUAL(33600, r.modem.model());
    r.feed("\r");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("> [3] Model"));
    r.feed("3\rq1A");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("q1A"));
    TEST_ASSERT_TRUE(r.terminal.isOpen());
    r.feed(
        "\x15"
        "28800\x1b[D\x1b[3~0\r");
    r.drain();
    r.feed("y\r");
    r.drain();
    TEST_ASSERT_EQUAL(28800, r.modem.model());
}
void test_phonebook_paging_details_selection_and_storage_failure() {
    Rig r;
    BookServices services;
    for (unsigned i = 0; i < 12; ++i)
        services.add(i);
    r.terminal.services(&services);
    r.terminal.setStyle("CP437");
    r.terminal.color(Color::Rgb);
    r.terminal.open();
    r.drain();
    r.feed("2\r");
    ScreenView view;
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("host00.example"));
    TEST_ASSERT_TRUE(view.has("host08.example"));
    TEST_ASSERT_FALSE(view.has("host09.example"));
    view.snapshot("phonebook-80x25");
    r.feed("\x1b[6~");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("PAGE 2/2"));
    TEST_ASSERT_TRUE(view.has("host09.example"));
    r.feed("i");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("Host: host09.example"));
    r.feed("\x1b");
    r.terminal.poll(100);
    r.drain();
    r.feed("3\r");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("Y confirms"));
    TEST_ASSERT_NOT_NULL(services.book.find("209"));
    r.feed("n\r");
    r.drain();
    services.store.fail = true;
    r.feed("3\ry\r");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("ERROR"));
    TEST_ASSERT_NOT_NULL(services.book.find("209"));
    r.feed("\r");
    r.drain();
    services.store.fail = false;
    r.feed("3\ry\r");
    r.drain();
    TEST_ASSERT_NULL(services.book.find("209"));
    r.feed("\r");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("host10.example"));
    TEST_ASSERT_TRUE(view.has("Link: Idle"));
}
void test_phonebook_empty_missing_hook_and_changed_entry_confirmation() {
    Rig r;
    r.terminal.open();
    r.drain();
    r.feed("2\r");
    ScreenView view;
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("Phonebook unavailable"));
    BookServices services;
    r.terminal.services(&services);
    r.terminal.poll(2100);
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("No saved entries"));
    r.feed("4\r");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("No selected entry"));
    services.add(0);
    r.terminal.poll(4200);
    r.drain();
    r.feed("4\r");
    r.drain();
    auto updated = *services.book.find("200");
    strcpy(updated.host, "changed.example");
    services.book.put(updated);
    r.feed("y\r");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("Entry changed"));
    TEST_ASSERT_TRUE(r.terminal.isOpen());
    r.feed("4\ry\r");
    char command[128];
    TEST_ASSERT_TRUE(r.terminal.takeDeparture(command, sizeof command));
    TEST_ASSERT_EQUAL_STRING("*DIAL=200", command);
}
void test_resize_mid_escape_and_secret_confirmation_timeout() {
    Rig r;
    r.terminal.resize(132, 50);
    r.terminal.open();
    uint8_t data[3];
    r.terminal.read(data, 3);
    ScreenView view;
    for (auto c : data)
        view.accept(c);
    r.terminal.resize(80, 25);
    r.feed("\x1b[C\x1b[B");
    view.accept(r.drain());
    TEST_ASSERT_EQUAL(0, view.outside);
    TEST_ASSERT_EQUAL(0, view.scrolls);
    TEST_ASSERT_TRUE(view.has("> [8] Updates and Reset"));
    TEST_ASSERT_TRUE(view.wrap && view.cursor);
    r.feed("8\r4\rTimeoutCanary\r");
    auto out = r.drain();
    TEST_ASSERT_EQUAL(std::string::npos, out.find("TimeoutCanary"));
    r.terminal.poll(31000);
    out = r.drain();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("timed out"));
    r.feed("y\r");
    out = r.drain();
    TEST_ASSERT_EQUAL(std::string::npos, out.find("TimeoutCanary"));
}
void test_cell_text_has_fixed_width_and_cannot_inject_controls() {
    {
        Rig r;
        r.terminal.open();
        uint8_t prefix[20];
        TEST_ASSERT_EQUAL(sizeof prefix, r.terminal.read(prefix, sizeof prefix));
        ScreenView view(79, 25);
        for (auto byte : prefix)
            view.accept(byte);
        r.terminal.setStyle("PLAIN");
        r.terminal.resize(79, 25);
        view.accept(r.drain());
        TEST_ASSERT_TRUE(view.wrap && view.cursor);
        TEST_ASSERT_EQUAL(0, view.outside);
    }
    {
        Rig r;
        r.terminal.setStyle("PLAIN");
        r.terminal.open();
        r.drain();
        r.terminal.poll(6000);
        TEST_ASSERT_TRUE(r.drain().empty());
        r.terminal.close();
        TEST_ASSERT_EQUAL(std::string::npos, r.sink.text.find('\x1b'));
    }
    for (auto size : {std::pair<unsigned, unsigned>{1, 1}, {10, 4}, {40, 5}}) {
        Rig r;
        r.terminal.resize(size.first, size.second);
        r.terminal.open();
        ScreenView view(size.first, size.second);
        view.accept(r.drain());
        TEST_ASSERT_EQUAL(0, view.outside);
        TEST_ASSERT_EQUAL(0, view.scrolls);
        r.feed("3\r3\r28800\r");
        view.accept(r.drain());
        TEST_ASSERT_EQUAL(0, view.outside);
        TEST_ASSERT_EQUAL(0, view.scrolls);
        r.feed("n\r");
        r.drain();
    }

    CellGrid grid;
    grid.clear(80, 25);
    grid.text(0, 0,
              "A\xcc\x81\xe7\x95\x8c"
              "B\x1b[2J");
    char bytes[4096];
    size_t n = grid.row(0, bytes, sizeof bytes, false, true, Color::Ansi16, 0);
    std::string line(bytes, n);
    TEST_ASSERT_EQUAL(0, line.find("A?B?[2J"));
    TEST_ASSERT_EQUAL(82, n);
}
