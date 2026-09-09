find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(test_byteformat test_byteformat.cpp)
target_link_libraries(test_byteformat PRIVATE asahidisksleuth-core Qt6::Core Qt6::Test)
add_test(NAME byteformat COMMAND test_byteformat)

add_executable(test_scan test_scan.cpp)
target_link_libraries(test_scan PRIVATE asahidisksleuth-core Qt6::Core Qt6::Test)
add_test(NAME scan COMMAND test_scan)

add_executable(test_reflink test_reflink.cpp)
target_link_libraries(test_reflink PRIVATE asahidisksleuth-core Qt6::Core Qt6::Test)
add_test(NAME reflink COMMAND test_reflink)
