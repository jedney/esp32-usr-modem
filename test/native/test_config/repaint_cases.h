// What one key costs on the line, and which changes still send a full frame.
#pragma once

struct KeyCost {
    const char *style;
    Color color;
    size_t open, arrow;
};

// Opens an 80 x 25 screen in `style`/`color` and presses Down once on the main menu.
KeyCost measureArrow(const char *style, Color color) {
    Rig r;
    r.terminal.setStyle(style);
    r.terminal.color(color);
    r.terminal.resize(80, 25);
    r.terminal.open();
    KeyCost cost{style, color, r.drain(64).size(), 0};
    r.feed("\x1b[B");
    cost.arrow = r.drain(64).size();
    return cost;
}

// One second at 2400 bit/s (8N1, 10 bits a byte): the limit for a key (decisions log).
constexpr size_t kKeyBound = 240;

void test_measure_one_arrow_key_at_80x25() {
    const KeyCost costs[] = {measureArrow("ANSI", Color::Ansi16), measureArrow("ANSI", Color::Rgb),
                             measureArrow("CP437", Color::Ansi16),
                             measureArrow("CP437", Color::Rgb),
                             measureArrow("PLAIN", Color::Ansi16)};
    for (const auto &c : costs) {
        char line[160];
        snprintf(line, sizeof line,
                 "%-5s %-6s open %5zu B, arrow %5zu B = %.2f s at 300, %.2f s at 2400, %.2f s at "
                 "9600",
                 c.style, c.color == Color::Rgb ? "RGB" : "16", c.open, c.arrow, c.arrow / 30.0,
                 c.arrow / 240.0, c.arrow / 960.0);
        TEST_MESSAGE(line);
        if (strcmp(c.style, "PLAIN"))
            TEST_ASSERT_LESS_OR_EQUAL(kKeyBound, c.arrow);
        else
            TEST_ASSERT_EQUAL(c.open, c.arrow);  // no cursor addressing: always a full frame
    }
}

// Every move through the main menu, both ways, stays under the bound.
void test_every_main_menu_move_sends_only_the_change() {
    for (Color color : {Color::Ansi16, Color::Ansi256, Color::Rgb}) {
        Rig r;
        r.terminal.setStyle("CP437");
        r.terminal.color(color);
        r.terminal.open();
        r.drain();
        for (const char *key : {"\x1b[B", "\x1b[A"})
            for (unsigned i = 0; i < 14; ++i) {
                r.feed(key);
                auto out = r.drain(64);
                TEST_ASSERT_LESS_OR_EQUAL(kKeyBound, out.size());
                TEST_ASSERT_EQUAL(std::string::npos, out.find("\x1b[2J"));
            }
    }
}

bool fullFrame(const std::string &out) {
    return out.find("\x1b[2J") != std::string::npos && out.size() > 2000;
}

void test_resize_refresh_style_colour_and_port_output_send_a_full_frame() {
    Rig r;
    r.terminal.open();
    r.drain();
    r.feed("\x1b[B");
    TEST_ASSERT_FALSE(fullFrame(r.drain()));
    r.terminal.resize(100, 30);  // also what a size probe answer does
    TEST_ASSERT_TRUE(fullFrame(r.drain()));
    r.feed("\x0c");  // Ctrl-L
    TEST_ASSERT_TRUE(fullFrame(r.drain()));
    r.terminal.setStyle("CP437");
    TEST_ASSERT_TRUE(fullFrame(r.drain()));
    r.terminal.color(Color::Rgb);
    TEST_ASSERT_TRUE(fullFrame(r.drain()));
    // Anything written to the port behind the screen (an unsolicited result, a hardware
    // test) leaves the terminal unknown: the next frame is full.
    r.session.port().print("RING\r\n");
    r.feed("\x1b[B");
    TEST_ASSERT_TRUE(fullFrame(r.drain()));
    r.feed("\x1b[B");
    TEST_ASSERT_FALSE(fullFrame(r.drain()));
    r.terminal.repaint();  // what the firmware calls after a hardware test
    TEST_ASSERT_TRUE(fullFrame(r.drain()));
    // A theme change recolours cells whose glyphs are the same.
    r.feed("\x1b[A");
    r.drain();
    TEST_ASSERT_TRUE(r.modem.setModel(33600));
    r.terminal.poll(2100);
    TEST_ASSERT_TRUE(fullFrame(r.drain()));
    // Esc out of a running command prints on the port: full frame again.
    r.terminal.escaped();
    TEST_ASSERT_TRUE(fullFrame(r.drain()));
}

// The screen built from changes must equal a full repaint of the same frame, glyphs and
// colours, through a walk over menus, forms, help, the phonebook and pages.
void expectSameAsFullRepaint(Rig &r, ScreenView &view, const char *step) {
    view.accept(r.drain(7));
    r.terminal.repaint();
    auto full = r.drain(64);
    TEST_ASSERT_TRUE_MESSAGE(fullFrame(full) || full.find("\x1b[2J") != std::string::npos, step);
    ScreenView truth(view.width, view.height);
    truth.accept(full);
    for (unsigned y = 0; y < view.height; ++y) {
        TEST_ASSERT_EQUAL_STRING_MESSAGE(truth.row(y).c_str(), view.row(y).c_str(), step);
        for (unsigned x = 0; x < view.width; ++x)
            TEST_ASSERT_EQUAL_HEX_MESSAGE(truth.color(x, y), view.color(x, y), step);
    }
    view.accept(full);
}

void test_changes_only_screen_matches_a_full_repaint() {
    for (auto size : {std::pair<unsigned, unsigned>{80, 25}, {132, 50}, {40, 12}}) {
        Rig r;
        BookServices services;
        for (unsigned i = 0; i < 12; ++i)
            services.add(i);
        r.terminal.services(&services);
        r.terminal.setStyle("CP437");
        r.terminal.color(Color::Rgb);
        r.terminal.resize(size.first, size.second);
        r.terminal.open();
        ScreenView view(size.first, size.second);
        view.accept(r.drain());
        const char *steps[] = {"\x1b[B", "\x1b[B",  "\x1b[A", "3\r",     "\x1b[B", "\x1b[B",
                               "3\r",    "288",     "\x7f",   "\x1b[D",  "\x1b",   "q",
                               "\x1bOP", "q",       "2\r",    "\x1b[6~", "\x1b[B", "i",
                               "\x1b",   "\x1b[5~", "q",      "1\r",     "q"};
        uint32_t now = 0;
        unsigned index = 0;
        for (const char *step : steps) {
            r.feed(step);
            r.terminal.poll(now += 100);  // a lone Esc is recognised after a pause
            char name[32];
            snprintf(name, sizeof name, "step %u", index++);
            TEST_ASSERT_TRUE_MESSAGE(r.terminal.isOpen(), name);
            expectSameAsFullRepaint(r, view, name);
        }
        TEST_ASSERT_EQUAL(0, view.outside);
        TEST_ASSERT_EQUAL(0, view.scrolls);
    }
}
