# PlatformIO supplies the selected application or embedded-test sources.
idf_component_register(SRCS "main.cpp" "rs232.cpp" "usbport.cpp" "hwconsole.cpp" INCLUDE_DIRS ".")

# Warnings for the project's own code only. Same list as scripts/project_warnings.py, which
# does the same for lib/. They come after IDF's flags on purpose: IDF turns some of these
# off for its own code.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wunused-parameter -Wsign-compare -Werror=deprecated-declarations)
