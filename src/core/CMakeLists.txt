add_library(asahidisksleuth-core STATIC
    util/ByteFormat.cpp
    util/ByteFormat.h
    model/NodeFlags.h
    model/FileGraph.h
    attrs/RawEntry.cpp
    attrs/RawEntry.h
    attrs/DirectoryReader.cpp
    attrs/DirectoryReader.h
    scan/GraphBuilder.cpp
    scan/GraphBuilder.h
    scan/DirHandle.h
    scan/VisitedSet.h
    scan/WorkQueue.h
    scan/ScanEngine.cpp
    scan/ScanEngine.h
    volume/VolumeInfo.cpp
    volume/VolumeInfo.h
)

target_include_directories(asahidisksleuth-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asahidisksleuth-core PUBLIC Qt6::Core)
target_compile_options(asahidisksleuth-core PRIVATE -Wall -Wextra -Wpedantic)
