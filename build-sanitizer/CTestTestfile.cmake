# CMake generated Testfile for 
# Source directory: /home/wissem/workspace/open-lotto
# Build directory: /home/wissem/workspace/open-lotto/build-sanitizer
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(unit_combogen "/home/wissem/workspace/open-lotto/build-sanitizer/test_combogen")
set_tests_properties(unit_combogen PROPERTIES  PASS_REGULAR_EXPRESSION "Tests passed:" _BACKTRACE_TRIPLES "/home/wissem/workspace/open-lotto/CMakeLists.txt;72;add_test;/home/wissem/workspace/open-lotto/CMakeLists.txt;0;")
add_test(cli_lists_games "/home/wissem/workspace/open-lotto/build-sanitizer/open-lotto" "--list-games")
set_tests_properties(cli_lists_games PROPERTIES  PASS_REGULAR_EXPRESSION "Eurojackpot|EuroJackpot" _BACKTRACE_TRIPLES "/home/wissem/workspace/open-lotto/CMakeLists.txt;80;add_test;/home/wissem/workspace/open-lotto/CMakeLists.txt;0;")
add_test(cli_runs_lotto_once "/home/wissem/workspace/open-lotto/build-sanitizer/open-lotto" "--game" "Lotto 6aus49" "--draws" "1")
set_tests_properties(cli_runs_lotto_once PROPERTIES  PASS_REGULAR_EXPRESSION "Main:" _BACKTRACE_TRIPLES "/home/wissem/workspace/open-lotto/CMakeLists.txt;87;add_test;/home/wissem/workspace/open-lotto/CMakeLists.txt;0;")
add_test(cli_runs_eurojackpot_once "/home/wissem/workspace/open-lotto/build-sanitizer/open-lotto" "--game" "Eurojackpot" "--draws" "1")
set_tests_properties(cli_runs_eurojackpot_once PROPERTIES  PASS_REGULAR_EXPRESSION "Main:" _BACKTRACE_TRIPLES "/home/wissem/workspace/open-lotto/CMakeLists.txt;94;add_test;/home/wissem/workspace/open-lotto/CMakeLists.txt;0;")
