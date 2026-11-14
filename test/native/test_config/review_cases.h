// Paging on tall screens, Alt+letter, and the "input too long" flag.
#pragma once

// Writes `count` numbered lines as command output and returns the screen after it.
void writeLines(Rig &r, ScreenView &view, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        char row[24];
        snprintf(row, sizeof row, "line%03u\n", i);
        r.terminal.write(row, strlen(row));
    }
    view.accept(r.drain());
}

// A page of output moves by exactly the lines the view shows: none repeated, none skipped.
void test_output_pages_step_by_the_lines_shown() {
    for (auto size : {std::pair<unsigned, unsigned>{132, 50}, {80, 25}, {80, 24}, {40, 12}}) {
        Rig r;
        r.terminal.resize(size.first, size.second);
        r.terminal.open();
        ScreenView view(size.first, size.second);
        view.accept(r.drain());
        writeLines(r, view, 200);
        unsigned shown = 0;
        while (shown < 200 && view.has(std::string("line") +
                                       (shown < 10    ? "00"
                                        : shown < 100 ? "0"
                                                      : "") +
                                       std::to_string(shown)))
            ++shown;
        TEST_ASSERT_GREATER_THAN(1, shown);
        r.feed("\r");
        view.accept(r.drain());
        char last[16], next[16];
        snprintf(last, sizeof last, "line%03u", shown - 1);
        snprintf(next, sizeof next, "line%03u", shown);
        TEST_ASSERT_FALSE_MESSAGE(view.has(last), last);  // not shown again
        TEST_ASSERT_TRUE_MESSAGE(view.has(next), next);   // nothing skipped
        r.feed("\x1b[5~");
        view.accept(r.drain());
        TEST_ASSERT_TRUE(view.has("line000"));
        TEST_ASSERT_TRUE_MESSAGE(view.has(last), last);
    }
}

// Esc and a letter together (what Alt+letter sends) neither cancels nor acts as a hotkey.
void test_alt_letter_neither_cancels_a_form_nor_runs_a_hotkey() {
    Rig r;
    r.terminal.open();
    ScreenView view;
    view.accept(r.drain());
    r.feed("3\r3\r");  // Settings, Model: a form
    r.feed(
        "\x15"
        "1");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("Value > 1 "));
    r.feed(
        "\x1b"
        "2");  // Alt+2 in one burst
    r.terminal.poll(100);
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("Value > 1 "));  // still the form, nothing added
    TEST_ASSERT_FALSE(view.has("Edit cancelled"));
    r.feed("\x1b");  // a lone Esc (with a pause) still cancels
    r.terminal.poll(200);
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("Edit cancelled"));
    r.feed("\x1b");
    r.terminal.poll(300);
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("MAIN MENU"));
    r.feed("\x1bq");  // Alt+q on the main menu must not close the screen
    r.terminal.poll(400);
    TEST_ASSERT_TRUE(r.terminal.isOpen());
    r.feed("\x1b");
    r.terminal.poll(500);
    TEST_ASSERT_FALSE(r.terminal.isOpen());
}

// The flag is deliberate: dropped characters are lost, so the field can't be sent until
// cleared (Ctrl-U) or cancelled. The screen says so as soon as a character is dropped.
void test_input_too_long_is_shown_at_once_and_kept_until_cleared() {
    Rig r;
    r.terminal.open();
    ScreenView view;
    view.accept(r.drain());
    r.feed("3\r3\r");
    view.accept(r.drain());
    std::string input(200, '1');
    r.feed(input.c_str());
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("Input too long"));
    r.feed("\x7f\x7f\r");  // Backspace does not bring the lost characters back
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("Input too long"));
    TEST_ASSERT_FALSE(view.has("Y confirms"));
    r.feed(
        "\x15"
        "28800\r");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("Y confirms"));
}
