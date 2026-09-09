add_executable(asahidisksleuth
    main.cpp
    Style.h
    ScanRunner.h
    TreePrinter.cpp
    TreePrinter.h
)

target_link_libraries(asahidisksleuth PRIVATE asahidisksleuth-core Qt6::Core)

install(TARGETS asahidisksleuth RUNTIME DESTINATION bin)
